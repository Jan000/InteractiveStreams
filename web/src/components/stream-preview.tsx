"use client";

import { useFrameStream } from "@/hooks/use-status";
import { Badge } from "@/components/ui/badge";
import { cn } from "@/lib/utils";

interface StreamPreviewProps {
  streamId: string | null;
  fps?: number;
  className?: string;
  /** Aspect ratio class, e.g. "aspect-[9/16]" for mobile or "aspect-video" for desktop */
  aspectClass?: string;
}

export function StreamPreview({
  streamId,
  fps = 10,
  className,
  aspectClass = "aspect-[9/16]",
}: StreamPreviewProps) {
  const { canvasRef, connected } = useFrameStream(streamId, fps);

  return (
    <div
      className={cn(
        "relative overflow-hidden rounded-lg border border-border bg-black",
        aspectClass,
        className
      )}
    >
      <canvas
        ref={canvasRef}
        className="absolute inset-0 h-full w-full object-contain"
      />

      {/* Connection indicator */}
      <div className="absolute right-2 top-2">
        <Badge
          variant={connected ? "default" : "secondary"}
          className={cn(
            "text-[10px] px-1.5 py-0",
            connected && "bg-green-600 hover:bg-green-600"
          )}
        >
          {connected ? "LIVE" : "NO SIGNAL"}
        </Badge>
      </div>

      {/* Placeholder when no stream selected */}
      {!streamId && (
        <div className="absolute inset-0 flex items-center justify-center text-sm text-muted-foreground">
          Select a stream to preview
        </div>
      )}
    </div>
  );
}
