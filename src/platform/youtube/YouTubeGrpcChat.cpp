#ifdef IS_YOUTUBE_GRPC_ENABLED

#include "platform/youtube/YouTubeGrpcChat.h"
#include "platform/youtube/YouTubeQuota.h"

// Generated proto headers (in build/proto-gen/)
#include "youtube_chat.pb.h"
#include "youtube_chat.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

namespace is::platform {

// ── Construction / Destruction ───────────────────────────────────────────────

YouTubeGrpcChat::YouTubeGrpcChat() = default;

YouTubeGrpcChat::~YouTubeGrpcChat() {
    stop();
}

// ── Public API ───────────────────────────────────────────────────────────────

void YouTubeGrpcChat::start(const std::string& apiKey,
                            const std::string& oauthToken,
                            const std::string& liveChatId,
                            const std::string& channelId,
                            MessageCallback onMessage) {
    if (m_running.load()) {
        spdlog::warn("[YouTube gRPC] Already running – call stop() first.");
        return;
    }

    m_apiKey      = apiKey;
    m_oauthToken  = oauthToken;
    m_liveChatId  = liveChatId;
    m_channelId   = channelId;
    m_onMessage   = std::move(onMessage);
    m_nextPageToken.clear();
    m_messagesReceived = 0;

    m_running = true;
    m_thread = std::thread(&YouTubeGrpcChat::streamLoop, this);

    spdlog::info("[YouTube gRPC] Started streaming for liveChatId '{}'.", liveChatId);
}

void YouTubeGrpcChat::stop() {
    if (!m_running.load()) return;

    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_connected = false;

    spdlog::info("[YouTube gRPC] Stopped.");
}

void YouTubeGrpcChat::updateToken(const std::string& newToken) {
    std::lock_guard<std::mutex> lock(m_tokenMutex);
    m_oauthToken = newToken;
}

// ── Background Streaming Loop ────────────────────────────────────────────────

void YouTubeGrpcChat::streamLoop() {
    // The outer loop handles reconnection.
    // Each iteration opens a new gRPC stream.
    // When the stream ends (offline, error, token expiry), we wait and retry.

    constexpr int RECONNECT_WAIT_SEC      = 5;
    constexpr int MAX_CONSECUTIVE_ERRORS  = 5;   // give up after N errors without messages
    constexpr int MAX_NOT_FOUND_RETRIES   = 3;

    size_t reconnects        = 0;
    int    consecutiveErrors = 0;
    int    notFoundRetries   = 0;

    while (m_running.load()) {
        // ── Create channel & stub ────────────────────────────────────────
        auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
        auto channel = grpc::CreateChannel("youtube.googleapis.com:443", channelCreds);

        auto stub = youtube::api::v3::V3DataLiveChatMessageService::NewStub(channel);

        // ── Build request ────────────────────────────────────────────────
        youtube::api::v3::LiveChatMessageListRequest request;
        request.set_live_chat_id(m_liveChatId);
        request.add_part("snippet");
        request.add_part("authorDetails");
        request.set_max_results(200);

        if (!m_nextPageToken.empty()) {
            request.set_page_token(m_nextPageToken);
        }

        // ── Authentication metadata ──────────────────────────────────────
        grpc::ClientContext context;

        std::string token;
        {
            std::lock_guard<std::mutex> lock(m_tokenMutex);
            token = m_oauthToken;
        }

        if (!token.empty()) {
            context.AddMetadata("authorization", "Bearer " + token);
        } else if (!m_apiKey.empty()) {
            context.AddMetadata("x-goog-api-key", m_apiKey);
        } else {
            spdlog::error("[YouTube gRPC] No API key or OAuth token – cannot authenticate.");
            m_running = false;
            break;
        }

        // ── Log gRPC connection in quota tracker ─────────────────────────
        // gRPC StreamList costs quota on Google's side (equivalent to
        // liveChatMessages.list).  We log it so the dashboard has visibility.
        YouTubeQuota::instance().consume(YouTubeQuota::COST_LIST, "gRPC StreamList (connect)");

        // ── Open server-streamed RPC ─────────────────────────────────────
        if (reconnects == 0) {
            spdlog::info("[YouTube gRPC] Opening stream (pageToken='{}') …",
                         m_nextPageToken.empty() ? "(none)" : m_nextPageToken);
        } else {
            spdlog::debug("[YouTube gRPC] Reopening stream #{} (pageToken='{}') …",
                          reconnects, m_nextPageToken.empty() ? "(none)" : m_nextPageToken);
        }

        auto reader = stub->StreamList(&context, request);
        m_connected = true;

        // ── Read responses ───────────────────────────────────────────────
        youtube::api::v3::LiveChatMessageListResponse response;
        size_t batchMessages = 0;

        while (reader->Read(&response) && m_running.load()) {
            // Update page token for reconnection continuity
            if (response.has_next_page_token()) {
                m_nextPageToken = response.next_page_token();
            }

            // Check if the stream went offline
            if (response.has_offline_at() && !response.offline_at().empty()) {
                spdlog::info("[YouTube gRPC] Stream went offline at {}.", response.offline_at());
                break;
            }

            // Process each chat message
            for (const auto& item : response.items()) {
                // We only care about text messages and super chats
                // (skip tombstones, bans, retractions, etc.)
                if (!item.has_snippet()) continue;

                const auto& snippet = item.snippet();

                // Check message type – only process displayable messages
                if (snippet.has_type()) {
                    auto type = snippet.type();
                    // Short enum aliases (TOMBSTONE, etc.) live on the wrapper
                    // *message* class, not on the enum type itself.
                    using T = youtube::api::v3::LiveChatMessageSnippet_TypeWrapper;
                    if (type == T::TOMBSTONE ||
                        type == T::CHAT_ENDED_EVENT ||
                        type == T::MESSAGE_DELETED_EVENT ||
                        type == T::MESSAGE_RETRACTED_EVENT ||
                        type == T::USER_BANNED_EVENT) {
                        // These are silent / moderation events – skip
                        if (type == T::CHAT_ENDED_EVENT) {
                            spdlog::info("[YouTube gRPC] Chat ended event received.");
                        }
                        continue;
                    }
                }

                // Extract display message text
                std::string text;
                if (snippet.has_display_message() && !snippet.display_message().empty()) {
                    text = snippet.display_message();
                } else if (snippet.has_text_message_details() &&
                           snippet.text_message_details().has_message_text()) {
                    text = snippet.text_message_details().message_text();
                }

                // Super Chat comments
                if (text.empty() && snippet.has_super_chat_details() &&
                    snippet.super_chat_details().has_user_comment()) {
                    text = snippet.super_chat_details().user_comment();
                }

                if (text.empty()) continue;

                // Build ChatMessage
                ChatMessage msg;
                msg.platformId = "youtube";
                msg.channelId  = m_channelId;
                msg.text       = text;

                if (item.has_author_details()) {
                    const auto& author = item.author_details();
                    std::string rawId = author.has_channel_id() ? author.channel_id() : "unknown";
                    msg.userId      = ChatMessage::makeUserId("youtube", rawId);
                    msg.displayName = author.has_display_name() ? author.display_name() : "Unknown";
                    msg.isModerator = author.has_is_chat_moderator() && author.is_chat_moderator();
                    msg.isSubscriber = (author.has_is_chat_sponsor() && author.is_chat_sponsor()) ||
                                      (author.has_is_chat_owner() && author.is_chat_owner());
                } else if (snippet.has_author_channel_id()) {
                    msg.userId      = ChatMessage::makeUserId("youtube", snippet.author_channel_id());
                    msg.displayName = snippet.author_channel_id();
                }

                m_messagesReceived++;
                batchMessages++;

                // Deliver via callback
                if (m_onMessage) {
                    m_onMessage(std::move(msg));
                }
            }
        }

        // ── Stream ended ─────────────────────────────────────────────────
        m_connected = false;

        // If we received messages this batch, reset the error counter
        if (batchMessages > 0) {
            consecutiveErrors = 0;
            notFoundRetries   = 0;
        }

        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            consecutiveErrors++;

            spdlog::warn("[YouTube gRPC] Stream ended with error: {} (code {})",
                         status.error_message(),
                         static_cast<int>(status.error_code()));

            // UNAUTHENTICATED / PERMISSION_DENIED – likely token expired
            if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
                status.error_code() == grpc::StatusCode::PERMISSION_DENIED) {
                spdlog::error("[YouTube gRPC] Authentication failed – "
                              "token likely expired, will reconnect with refreshed token.");
                for (int i = 0; i < 5 && m_running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                    spdlog::error("[YouTube gRPC] Too many consecutive auth errors ({}) – "
                                  "stopping gRPC, falling back to REST.", consecutiveErrors);
                    break;
                }
                continue;
            }

            // NOT_FOUND – liveChatId is invalid or stream ended
            if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
                notFoundRetries++;
                if (notFoundRetries >= MAX_NOT_FOUND_RETRIES) {
                    spdlog::warn("[YouTube gRPC] Live chat not found after {} retries – "
                                 "stopping gRPC (stream likely ended).", notFoundRetries);
                    break;
                }
                spdlog::warn("[YouTube gRPC] Live chat not found (attempt {}/{}) – "
                             "retrying in 30s…", notFoundRetries, MAX_NOT_FOUND_RETRIES);
                for (int i = 0; i < 30 && m_running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                continue;
            }

            // RESOURCE_EXHAUSTED – quota exceeded: stop immediately
            if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED) {
                spdlog::error("[YouTube gRPC] Quota exhausted – stopping gRPC stream. "
                              "Will fall back to REST (which respects local quota budget).");
                YouTubeQuota::instance().consume(0, "gRPC RESOURCE_EXHAUSTED (quota hit)");
                break;
            }

            // Any other error
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                spdlog::error("[YouTube gRPC] Too many consecutive errors ({}) – "
                              "stopping gRPC, falling back to REST.", consecutiveErrors);
                break;
            }
        } else {
            spdlog::debug("[YouTube gRPC] Stream batch ended normally ({} msgs).", batchMessages);
        }

        reconnects++;

        // Brief wait before reconnecting
        if (m_running.load()) {
            spdlog::debug("[YouTube gRPC] Reconnecting in {} seconds…", RECONNECT_WAIT_SEC);
            for (int i = 0; i < RECONNECT_WAIT_SEC && m_running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    m_connected = false;
    spdlog::info("[YouTube gRPC] Stream loop exited.");
}

} // namespace is::platform

#endif // IS_YOUTUBE_GRPC_ENABLED
