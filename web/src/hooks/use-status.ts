"use client";

import { useEffect, useRef, useState, useCallback } from "react";
import { api, type StatusResponse } from "@/lib/api";

/**
 * Polls /api/status at `interval` ms and returns the latest data.
 */
export function useStatus(interval = 1000) {
  const [data, setData] = useState<StatusResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    const poll = async () => {
      while (active) {
        try {
          const status = await api.getStatus();
          if (active) {
            setData(status);
            setError(null);
          }
        } catch (e) {
          if (active) setError(e instanceof Error ? e.message : "Connection lost");
        }
        await new Promise((r) => setTimeout(r, interval));
      }
    };
    poll();
    return () => { active = false; };
  }, [interval]);

  return { data, error };
}

/**
 * Streams JPEG frames from /api/streams/:id/frame onto a <canvas>.
 * Returns a ref to attach to the canvas element.
 */
export function useFrameStream(streamId: string | null, fps = 10) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    if (!streamId) return;
    let active = true;
    setConnected(false);

    const fetchFrames = async () => {
      while (active) {
        try {
          const res = await fetch(api.frameUrl(streamId) + `?t=${Date.now()}`);
          if (!res.ok) throw new Error("No frame");
          const blob = await res.blob();
          const bitmap = await createImageBitmap(blob);
          const ctx = canvasRef.current?.getContext("2d");
          if (ctx && canvasRef.current) {
            // Only reset canvas dimensions when they actually change
            // (resetting clears the canvas and causes DOM thrashing)
            if (
              canvasRef.current.width !== bitmap.width ||
              canvasRef.current.height !== bitmap.height
            ) {
              canvasRef.current.width = bitmap.width;
              canvasRef.current.height = bitmap.height;
            }
            ctx.drawImage(bitmap, 0, 0);
            setConnected(true);
          }
          bitmap.close();
        } catch {
          setConnected(false);
        }
        await new Promise((r) => setTimeout(r, 1000 / fps));
      }
    };
    fetchFrames();
    return () => { active = false; };
  }, [streamId, fps]);

  return { canvasRef, connected };
}

/**
 * Simple toast-like notification (uses sonner under the hood).
 */
export function useApiAction() {
  const [loading, setLoading] = useState(false);

  const run = useCallback(async <T>(fn: () => Promise<T>): Promise<T | null> => {
    setLoading(true);
    try {
      const result = await fn();
      return result;
    } catch (e) {
      console.error(e);
      return null;
    } finally {
      setLoading(false);
    }
  }, []);

  return { run, loading };
}
