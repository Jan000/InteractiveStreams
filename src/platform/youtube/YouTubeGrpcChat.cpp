#ifdef IS_YOUTUBE_GRPC_ENABLED

#include "platform/youtube/YouTubeGrpcChat.h"

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

// ── Background Streaming Loop ────────────────────────────────────────────────

void YouTubeGrpcChat::streamLoop() {
    // The outer loop handles reconnection.
    // Each iteration opens a new gRPC stream.
    // When the stream ends (offline, error, token expiry), we wait and retry.

    constexpr int RECONNECT_WAIT_SEC = 5;

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

        if (!m_oauthToken.empty()) {
            context.AddMetadata("authorization", "Bearer " + m_oauthToken);
        } else if (!m_apiKey.empty()) {
            context.AddMetadata("x-goog-api-key", m_apiKey);
        } else {
            spdlog::error("[YouTube gRPC] No API key or OAuth token – cannot authenticate.");
            m_running = false;
            break;
        }

        // ── Open server-streamed RPC ─────────────────────────────────────
        spdlog::info("[YouTube gRPC] Opening stream (pageToken='{}') …",
                     m_nextPageToken.empty() ? "(none)" : m_nextPageToken);

        auto reader = stub->StreamList(&context, request);
        m_connected = true;

        // ── Read responses ───────────────────────────────────────────────
        youtube::api::v3::LiveChatMessageListResponse response;

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

                // Deliver via callback
                if (m_onMessage) {
                    m_onMessage(std::move(msg));
                }
            }
        }

        // ── Stream ended ─────────────────────────────────────────────────
        m_connected = false;

        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            spdlog::warn("[YouTube gRPC] Stream ended with error: {} (code {})",
                         status.error_message(),
                         static_cast<int>(status.error_code()));

            // UNAUTHENTICATED / PERMISSION_DENIED – likely token expired
            if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
                status.error_code() == grpc::StatusCode::PERMISSION_DENIED) {
                spdlog::error("[YouTube gRPC] Authentication failed. "
                              "Check your API key / OAuth token.");
                // Don't retry immediately – the token needs to be refreshed
                for (int i = 0; i < 60 && m_running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                continue;
            }

            // NOT_FOUND – liveChatId is invalid or stream ended
            if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
                spdlog::warn("[YouTube gRPC] Live chat not found (stream may have ended). "
                             "Retrying in 30 seconds…");
                for (int i = 0; i < 30 && m_running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                continue;
            }

            // RESOURCE_EXHAUSTED – quota exceeded
            if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED) {
                spdlog::warn("[YouTube gRPC] Quota exceeded. Waiting 60 seconds…");
                for (int i = 0; i < 60 && m_running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                continue;
            }
        } else {
            spdlog::info("[YouTube gRPC] Stream ended normally (server closed connection).");
        }

        // Brief wait before reconnecting
        if (m_running.load()) {
            spdlog::info("[YouTube gRPC] Reconnecting in {} seconds…", RECONNECT_WAIT_SEC);
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
