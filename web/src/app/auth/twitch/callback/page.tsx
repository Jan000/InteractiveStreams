"use client";

import { useEffect, useState } from "react";

/**
 * Twitch OAuth Implicit Grant Flow – Callback Page
 *
 * Twitch redirects here with the access token in the URL fragment:
 *   /auth/twitch/callback/#access_token=xxx&scope=...&state=channelId&token_type=bearer
 *
 * This page:
 * 1. Parses the fragment to extract the token and channel ID (state)
 * 2. Sends the token back to the opener window via postMessage
 * 3. Closes itself (or shows a success/error message)
 */
export default function TwitchCallbackPage() {
  const [status, setStatus] = useState<"loading" | "success" | "error">(
    "loading"
  );
  const [errorMessage, setErrorMessage] = useState("");

  useEffect(() => {
    try {
      const hash = window.location.hash.substring(1); // remove leading #
      const params = new URLSearchParams(hash);

      const accessToken = params.get("access_token");
      const state = params.get("state"); // channel ID
      const error = params.get("error");
      const errorDescription = params.get("error_description");

      if (error) {
        setStatus("error");
        setErrorMessage(errorDescription || error);
        return;
      }

      if (!accessToken) {
        setStatus("error");
        setErrorMessage("No access token received from Twitch.");
        return;
      }

      // Send token back to opener (the channel card that opened this popup)
      if (window.opener) {
        window.opener.postMessage(
          {
            type: "twitch-oauth",
            accessToken,
            channelId: state || "",
          },
          window.location.origin
        );
        setStatus("success");

        // Auto-close popup after a short delay
        setTimeout(() => window.close(), 1500);
      } else {
        // If no opener (e.g. direct navigation), show the token info
        setStatus("success");
      }
    } catch (e) {
      setStatus("error");
      setErrorMessage(
        e instanceof Error ? e.message : "Unknown error occurred"
      );
    }
  }, []);

  return (
    <div className="flex min-h-screen items-center justify-center bg-zinc-950 text-white">
      <div className="w-full max-w-md rounded-xl border border-zinc-800 bg-zinc-900 p-8 text-center shadow-lg">
        {/* Twitch logo */}
        <div className="mb-6">
          <svg
            className="mx-auto h-12 w-12 text-purple-400"
            viewBox="0 0 24 24"
            fill="currentColor"
          >
            <path d="M11.571 4.714h1.715v5.143H11.57zm4.715 0H18v5.143h-1.714zM6 0L1.714 4.286v15.428h5.143V24l4.286-4.286h3.428L22.286 12V0zm14.571 11.143l-3.428 3.428h-3.429l-3 3v-3H6.857V1.714h13.714z" />
          </svg>
        </div>

        {status === "loading" && (
          <>
            <h1 className="text-xl font-semibold">Connecting to Twitch...</h1>
            <p className="mt-2 text-zinc-400">Processing authorization...</p>
            <div className="mt-6">
              <div className="mx-auto h-8 w-8 animate-spin rounded-full border-2 border-zinc-700 border-t-purple-400" />
            </div>
          </>
        )}

        {status === "success" && (
          <>
            <h1 className="text-xl font-semibold text-green-400">
              Authorization Successful!
            </h1>
            <p className="mt-2 text-zinc-400">
              Your Twitch account has been connected.
              {window.opener
                ? " This window will close automatically..."
                : " You can close this window."}
            </p>
            <div className="mt-6 text-4xl">✓</div>
          </>
        )}

        {status === "error" && (
          <>
            <h1 className="text-xl font-semibold text-red-400">
              Authorization Failed
            </h1>
            <p className="mt-2 text-zinc-400">{errorMessage}</p>
            <button
              onClick={() => window.close()}
              className="mt-6 rounded-lg bg-zinc-800 px-4 py-2 text-sm font-medium text-white hover:bg-zinc-700"
            >
              Close Window
            </button>
          </>
        )}
      </div>
    </div>
  );
}
