"use client";

import { useEffect, useState } from "react";
import { api } from "@/lib/api";

/**
 * YouTube OAuth Authorization Code Flow – Callback Page
 *
 * Google redirects here with the authorization code in the URL query string:
 *   /auth/youtube/callback/?code=xxx&state=channelId
 *
 * This page:
 * 1. Parses the query params to extract the code and channel ID (state)
 * 2. Sends the code to the backend which exchanges it for tokens
 * 3. Reports success/error to the opener window via postMessage
 * 4. Closes itself (or shows a success/error message)
 */
export default function YouTubeCallbackPage() {
  const [status, setStatus] = useState<"loading" | "success" | "error">(
    "loading"
  );
  const [errorMessage, setErrorMessage] = useState("");

  useEffect(() => {
    const doExchange = async () => {
      try {
        const params = new URLSearchParams(window.location.search);

        const code = params.get("code");
        const state = params.get("state"); // channel ID
        const error = params.get("error");

        if (error) {
          setStatus("error");
          setErrorMessage(
            params.get("error_description") || error
          );
          return;
        }

        if (!code) {
          setStatus("error");
          setErrorMessage("No authorization code received from Google.");
          return;
        }

        // Exchange the code for tokens via backend
        const result = await api.exchangeYouTubeCode(state || "", code);

        if (!result.success) {
          setStatus("error");
          setErrorMessage((result as Record<string, unknown>).error as string || "Token exchange failed");
          return;
        }

        // Notify opener
        if (window.opener) {
          window.opener.postMessage(
            {
              type: "youtube-oauth",
              channelId: state || "",
              success: true,
            },
            window.location.origin
          );
        }

        setStatus("success");
        setTimeout(() => window.close(), 1500);
      } catch (e) {
        setStatus("error");
        setErrorMessage(
          e instanceof Error ? e.message : "Unknown error occurred"
        );
      }
    };

    doExchange();
  }, []);

  return (
    <div className="flex min-h-screen items-center justify-center bg-zinc-950 text-white">
      <div className="w-full max-w-md rounded-xl border border-zinc-800 bg-zinc-900 p-8 text-center shadow-lg">
        {/* YouTube icon */}
        <div className="mb-6">
          <svg
            className="mx-auto h-12 w-12 text-red-500"
            viewBox="0 0 24 24"
            fill="currentColor"
          >
            <path d="M23.498 6.186a3.016 3.016 0 0 0-2.122-2.136C19.505 3.545 12 3.545 12 3.545s-7.505 0-9.377.505A3.017 3.017 0 0 0 .502 6.186C0 8.07 0 12 0 12s0 3.93.502 5.814a3.016 3.016 0 0 0 2.122 2.136c1.871.505 9.376.505 9.376.505s7.505 0 9.377-.505a3.015 3.015 0 0 0 2.122-2.136C24 15.93 24 12 24 12s0-3.93-.502-5.814zM9.545 15.568V8.432L15.818 12l-6.273 3.568z" />
          </svg>
        </div>

        {status === "loading" && (
          <>
            <h1 className="text-xl font-semibold">Connecting to YouTube...</h1>
            <p className="mt-2 text-zinc-400">
              Exchanging authorization code for tokens...
            </p>
            <div className="mt-6">
              <div className="mx-auto h-8 w-8 animate-spin rounded-full border-2 border-zinc-700 border-t-red-500" />
            </div>
          </>
        )}

        {status === "success" && (
          <>
            <h1 className="text-xl font-semibold text-green-400">
              Authorization Successful!
            </h1>
            <p className="mt-2 text-zinc-400">
              Your YouTube account has been connected. This window will close
              automatically.
            </p>
            <div className="mt-6 text-4xl">✅</div>
          </>
        )}

        {status === "error" && (
          <>
            <h1 className="text-xl font-semibold text-red-400">
              Authorization Failed
            </h1>
            <p className="mt-2 text-zinc-400">{errorMessage}</p>
            <button
              className="mt-6 rounded-lg bg-zinc-800 px-4 py-2 text-sm text-zinc-300 hover:bg-zinc-700"
              onClick={() => window.close()}
            >
              Close Window
            </button>
          </>
        )}
      </div>
    </div>
  );
}
