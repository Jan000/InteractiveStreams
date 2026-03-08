"use client";

import { useState, useEffect, useCallback } from "react";
import { Plus } from "lucide-react";
import { useStatus } from "@/hooks/use-status";
import { api, type StreamProfile } from "@/lib/api";
import { StreamCard } from "@/components/stream-card";
import { StreamPreview } from "@/components/stream-preview";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { toast } from "sonner";

const PREVIEW_SPEEDS: { label: string; fps: number }[] = [
  { label: "Fast (10 fps)", fps: 10 },
  { label: "Slow (2 fps)", fps: 2 },
  { label: "Off", fps: 0 },
];

export default function StreamsPage() {
  const { data, error, refresh } = useStatus(1000);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [previewFps, setPreviewFps] = useState(10);
  const [profiles, setProfiles] = useState<StreamProfile[]>([]);

  useEffect(() => {
    if (typeof window === "undefined") return;
    const saved = window.localStorage.getItem("is_preview_fps");
    if (saved === null) return;
    const parsed = Number(saved);
    if (Number.isFinite(parsed) && PREVIEW_SPEEDS.some((s) => s.fps === parsed)) {
      setPreviewFps(parsed);
    }
  }, []);

  useEffect(() => {
    if (typeof window === "undefined") return;
    window.localStorage.setItem("is_preview_fps", String(previewFps));
  }, [previewFps]);

  // Fetch profiles once and on refresh
  const loadProfiles = useCallback(async () => {
    try {
      setProfiles(await api.getProfiles());
    } catch { /* ignore */ }
  }, []);

  useEffect(() => {
    loadProfiles();
  }, [loadProfiles]);

  const handleRefresh = useCallback(() => {
    refresh();
    loadProfiles();
  }, [refresh, loadProfiles]);

  // New stream form state
  const [newName, setNewName] = useState("");
  const [newRes, setNewRes] = useState("mobile");
  const [newMode, setNewMode] = useState("vote");
  const [newGame, setNewGame] = useState("");

  const selectedStream =
    data?.streams.find((s) => s.id === selectedId) ?? data?.streams[0] ?? null;

  const handleCreateStream = async () => {
    try {
      const body: Record<string, unknown> = {
        name: newName || undefined,
        resolution: newRes,
        gameMode: newMode,
      };
      if (newGame) body.fixedGame = newGame;

      await api.createStream(body);
      toast.success("Stream created");
      setDialogOpen(false);
      setNewName("");
      handleRefresh();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to create stream");
    }
  };

  // Loading state
  if (!data && !error) {
    return (
      <div className="space-y-4">
        <Skeleton className="h-8 w-48" />
        <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
          {[1, 2].map((i) => (
            <Skeleton key={i} className="h-52" />
          ))}
        </div>
      </div>
    );
  }

  // Error state
  if (error) {
    return (
      <div className="flex flex-col items-center justify-center gap-4 py-20">
        <p className="text-sm text-destructive">Connection lost: {error}</p>
        <p className="text-xs text-muted-foreground">
          Make sure the C++ server is running on port 8080
        </p>
      </div>
    );
  }

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold tracking-tight">Streams</h1>
          <p className="text-sm text-muted-foreground">
            Manage streaming instances and preview live output
          </p>
        </div>
        <Dialog open={dialogOpen} onOpenChange={setDialogOpen}>
          <DialogTrigger asChild>
            <Button size="sm" className="gap-1">
              <Plus className="size-4" />
              New Stream
            </Button>
          </DialogTrigger>
          <DialogContent>
            <DialogHeader>
              <DialogTitle>Create Stream</DialogTitle>
            </DialogHeader>
            <div className="space-y-4 pt-2">
              <div className="space-y-2">
                <Label htmlFor="name">Name (optional)</Label>
                <Input
                  id="name"
                  placeholder="e.g. Main, Mobile…"
                  value={newName}
                  onChange={(e) => setNewName(e.target.value)}
                />
              </div>
              <div className="grid grid-cols-2 gap-4">
                <div className="space-y-2">
                  <Label>Resolution</Label>
                  <Select value={newRes} onValueChange={setNewRes}>
                    <SelectTrigger>
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="mobile">
                        Mobile (1080×1920)
                      </SelectItem>
                      <SelectItem value="desktop">
                        Desktop (1920×1080)
                      </SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="space-y-2">
                  <Label>Game Mode</Label>
                  <Select value={newMode} onValueChange={setNewMode}>
                    <SelectTrigger>
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="fixed">Fixed</SelectItem>
                      <SelectItem value="vote">Vote</SelectItem>
                      <SelectItem value="random">Random</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
              </div>
              {newMode === "fixed" && (
                <div className="space-y-2">
                  <Label>Game</Label>
                  <Select value={newGame} onValueChange={setNewGame}>
                    <SelectTrigger>
                      <SelectValue placeholder="Select game…" />
                    </SelectTrigger>
                    <SelectContent>
                      {data?.games.map((g) => (
                        <SelectItem key={g.id} value={g.id}>
                          {g.name}
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </div>
              )}
              <Button className="w-full" onClick={handleCreateStream}>
                Create
              </Button>
            </div>
          </DialogContent>
        </Dialog>
      </div>

      <Separator />

      {/* Content: Cards + Preview */}
      <div className="grid gap-6 lg:grid-cols-[1fr_340px]">
        {/* Stream cards */}
        <div className="space-y-4">
          {data?.streams.length === 0 && (
            <p className="py-12 text-center text-sm text-muted-foreground">
              No streams configured. Create one to get started.
            </p>
          )}
          {data?.streams.map((s) => (
            <StreamCard
              key={s.id}
              stream={s}
              games={data.games}
              channels={data.channels}
              profiles={profiles}
              selected={selectedStream?.id === s.id}
              onSelect={() => setSelectedId(s.id)}
              onRefresh={handleRefresh}
            />
          ))}
        </div>

        {/* Live preview sidebar */}
        <div className="space-y-3">
          <div className="flex items-center justify-between">
            <h2 className="text-sm font-semibold">Live Preview</h2>
            <div className="flex gap-1">
              {PREVIEW_SPEEDS.map((s) => (
                <Button
                  key={s.fps}
                  size="sm"
                  variant={previewFps === s.fps ? "default" : "ghost"}
                  className="h-6 px-2 text-[10px]"
                  onClick={() => setPreviewFps(s.fps)}
                >
                  {s.label}
                </Button>
              ))}
            </div>
          </div>
          <StreamPreview
            streamId={selectedStream?.id ?? null}
            fps={previewFps}
            aspectClass={
              selectedStream?.resolution === "desktop"
                ? "aspect-video"
                : "aspect-[9/16]"
            }
          />
          {selectedStream?.game && (
            <div className="rounded-lg border p-3 text-xs">
              <p className="font-medium">{selectedStream.game.name}</p>
              <p className="mt-1 text-muted-foreground">
                Round complete: {selectedStream.game.isRoundComplete ? "Yes" : "No"}
                {" · "}
                Game over: {selectedStream.game.isGameOver ? "Yes" : "No"}
              </p>
              {selectedStream.channelIds.length > 0 && (
                <p className="mt-1 text-muted-foreground">
                  Channels: {selectedStream.channelIds.join(", ")}
                </p>
              )}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
