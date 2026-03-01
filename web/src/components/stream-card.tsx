"use client";

import {
  MonitorPlay,
  Play,
  Square,
  Trash2,
  RefreshCw,
  Radio,
  Save,
  ChevronDown,
  ChevronUp,
  Settings2,
  Send,
  Tv,
  MessageSquare,
} from "lucide-react";
import type { StreamState, GameInfo, ChannelState } from "@/lib/api";
import { api } from "@/lib/api";
import { cn } from "@/lib/utils";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Textarea } from "@/components/ui/textarea";
import {
  Tooltip,
  TooltipTrigger,
  TooltipContent,
} from "@/components/ui/tooltip";
import { toast } from "sonner";
import { useState, useEffect, useCallback } from "react";

interface StreamCardProps {
  stream: StreamState;
  games: GameInfo[];
  channels: ChannelState[];
  selected?: boolean;
  onSelect?: () => void;
  onRefresh?: () => void;
}

const modeLabels: Record<string, string> = {
  fixed: "Fixed",
  vote: "Vote",
  random: "Random",
};

const resLabels: Record<string, string> = {
  mobile: "1080×1920",
  desktop: "1920×1080",
};

export function StreamCard({
  stream,
  games,
  channels,
  selected,
  onSelect,
  onRefresh,
}: StreamCardProps) {
  // Game switch state
  const [switchGame, setSwitchGame] = useState(stream.game?.id ?? "");
  const [switchMode, setSwitchMode] = useState("immediate");

  // Editable settings (initialized from stream props)
  const [name, setName] = useState(stream.name);
  const [title, setTitle] = useState(stream.title ?? "");
  const [description, setDescription] = useState(stream.description ?? "");
  const [resolution, setResolution] = useState(stream.resolution);
  const [gameMode, setGameMode] = useState(stream.gameMode);
  const [fixedGame, setFixedGame] = useState(stream.fixedGame ?? "");
  const [channelIds, setChannelIds] = useState<string[]>(stream.channelIds);
  const [streamUrl, setStreamUrl] = useState(stream.streamUrl ?? "");
  const [streamKey, setStreamKey] = useState(stream.streamKey ?? "");
  const [fps, setFps] = useState(stream.fps ?? 30);
  const [bitrate, setBitrate] = useState(stream.bitrate ?? 4500);
  const [preset, setPreset] = useState(stream.preset ?? "veryfast");
  const [codec, setCodec] = useState(stream.codec ?? "libx264");

  // UI state
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [chatOpen, setChatOpen] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);

  // Chat state
  const [chatUser, setChatUser] = useState("Player1");
  const [chatMsg, setChatMsg] = useState("");
  const [chatLog, setChatLog] = useState<string[]>([]);
  const [chatTarget, setChatTarget] = useState("local"); // "local", "all", or a channelId

  // Poll chat log when chat panel is open
  useEffect(() => {
    if (!chatOpen) return;
    let active = true;
    const poll = async () => {
      while (active) {
        try {
          const entries = await api.getChatLog();
          if (active) setChatLog(entries);
        } catch { /* ignore */ }
        await new Promise((r) => setTimeout(r, 1500));
      }
    };
    poll();
    return () => { active = false; };
  }, [chatOpen]);



  // Sync from server when stream changes (e.g. polling update)
  useEffect(() => {
    if (!dirty) {
      setName(stream.name);
      setTitle(stream.title ?? "");
      setDescription(stream.description ?? "");
      setResolution(stream.resolution);
      setGameMode(stream.gameMode);
      setFixedGame(stream.fixedGame ?? "");
      setChannelIds(stream.channelIds);
      setStreamUrl(stream.streamUrl ?? "");
      setStreamKey(stream.streamKey ?? "");
      setFps(stream.fps ?? 30);
      setBitrate(stream.bitrate ?? 4500);
      setPreset(stream.preset ?? "veryfast");
      setCodec(stream.codec ?? "libx264");
    }
  }, [stream, dirty]);

  // Mark dirty whenever a field changes
  const set = useCallback(
    <T,>(setter: React.Dispatch<React.SetStateAction<T>>) =>
      (val: T) => {
        setter(val);
        setDirty(true);
      },
    []
  );

  const handleSave = async () => {
    setSaving(true);
    try {
      await api.updateStream(stream.id, {
        name,
        title,
        description,
        resolution,
        game_mode: gameMode,
        fixed_game: fixedGame || undefined,
        channel_ids: channelIds,
        stream_url: streamUrl || undefined,
        stream_key: streamKey || undefined,
        fps,
        bitrate_kbps: bitrate,
        preset,
        codec,
      });
      toast.success("Stream updated");
      setDirty(false);
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to update");
    } finally {
      setSaving(false);
    }
  };

  const handleSwitchGame = async () => {
    if (!switchGame) return;
    try {
      await api.switchGame(stream.id, switchGame, switchMode);
      toast.success("Game switch requested");
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleStartStop = async () => {
    try {
      if (stream.streaming) {
        await api.stopStreaming(stream.id);
        toast.success("Streaming stopped");
      } else {
        // Validate locally first for instant feedback
        if (!stream.streamUrl && !streamUrl) {
          toast.error("No RTMP URL configured. Set stream URL and key in settings first.");
          setSettingsOpen(true);
          return;
        }
        await api.startStreaming(stream.id);
        toast.success("Streaming started");
      }
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to start streaming");
    }
  };

  const handleDelete = async () => {
    if (!confirm(`Delete stream "${stream.name}"?`)) return;
    try {
      await api.deleteStream(stream.id);
      toast.success("Stream deleted");
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const toggleChannel = (id: string) => {
    setDirty(true);
    setChannelIds((prev) =>
      prev.includes(id) ? prev.filter((c) => c !== id) : [...prev, id]
    );
  };

  const handleChatSend = async () => {
    const text = chatMsg.trim();
    if (!text) return;
    try {
      if (chatTarget === "local") {
        await api.sendChat(chatUser, text);
      } else if (chatTarget === "all") {
        await api.broadcastChat(text);
      } else {
        await api.sendToChannel(chatTarget, text);
      }
      setChatMsg("");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleQuickCmd = async (cmd: string) => {
    try {
      if (chatTarget === "local") {
        await api.sendChat(chatUser, cmd);
      } else if (chatTarget === "all") {
        await api.broadcastChat(cmd);
      } else {
        await api.sendToChannel(chatTarget, cmd);
      }
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const hasValidUrl = !!(stream.streamUrl || streamUrl);

  return (
    <Card
      className={cn(
        "transition-colors",
        selected
          ? "border-primary ring-1 ring-primary"
          : "hover:border-muted-foreground/40"
      )}
      onClick={onSelect}
    >
      <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
        <CardTitle className="flex items-center gap-2 text-sm font-medium">
          <MonitorPlay className="size-4" />
          {stream.name || stream.id}
          {stream.title && (
            <span className="text-xs font-normal text-muted-foreground">
              — {stream.title}
            </span>
          )}
        </CardTitle>
        <div className="flex items-center gap-1.5">
          <Badge variant="outline" className="text-[10px]">
            {modeLabels[stream.gameMode] ?? stream.gameMode}
          </Badge>
          <Badge variant="outline" className="text-[10px]">
            {resLabels[stream.resolution] ??
              `${stream.width}×${stream.height}`}
          </Badge>
          {stream.streaming && (
            <Badge className="bg-red-600 text-[10px] hover:bg-red-600">
              <Radio className="mr-1 size-3" />
              LIVE
            </Badge>
          )}
          {!hasValidUrl && (
            <Badge variant="secondary" className="text-[10px] text-yellow-500">
              No URL
            </Badge>
          )}
          {dirty && (
            <Badge variant="secondary" className="text-[10px]">
              unsaved
            </Badge>
          )}
        </div>
      </CardHeader>

      <CardContent className="space-y-3" onClick={(e) => e.stopPropagation()}>
        {/* Current game */}
        <div className="text-xs text-muted-foreground">
          Game:{" "}
          <span className="font-medium text-foreground">
            {stream.game?.name ?? "None"}
          </span>
          {stream.pendingSwitch && (
            <span className="ml-2 text-yellow-500">
              → {stream.pendingSwitch.game} ({stream.pendingSwitch.mode})
            </span>
          )}
        </div>

        {/* Vote info */}
        {stream.vote?.active && (
          <div className="rounded-md bg-accent p-2 text-xs">
            <p className="font-medium">
              Vote active — {Math.ceil(stream.vote.timer)}s remaining
            </p>
            <div className="mt-1 flex flex-wrap gap-2">
              {Object.entries(stream.vote.tallies).map(([game, count]) => (
                <Badge key={game} variant="secondary">
                  {game}: {count as number}
                </Badge>
              ))}
            </div>
          </div>
        )}

        {/* Game switch controls */}
        <div className="flex gap-2">
          <Select value={switchGame} onValueChange={setSwitchGame}>
            <SelectTrigger className="h-8 flex-1 text-xs">
              <SelectValue placeholder="Switch game…" />
            </SelectTrigger>
            <SelectContent>
              {games.map((g) => (
                <SelectItem key={g.id} value={g.id}>
                  {g.name}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          <Select value={switchMode} onValueChange={setSwitchMode}>
            <SelectTrigger className="h-8 w-[120px] text-xs">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="immediate">Immediate</SelectItem>
              <SelectItem value="after_round">After Round</SelectItem>
              <SelectItem value="after_game">After Game</SelectItem>
            </SelectContent>
          </Select>
          <Button
            size="sm"
            variant="secondary"
            className="h-8"
            onClick={handleSwitchGame}
          >
            <RefreshCw className="size-3.5" />
          </Button>
        </div>

        {/* ── Section toggles ──────────────────────────────────── */}
        <div className="flex gap-3">
          <button
            type="button"
            className="flex items-center gap-1.5 text-xs font-medium text-muted-foreground hover:text-foreground transition-colors"
            onClick={() => setSettingsOpen((v) => !v)}
          >
            <Settings2 className="size-3.5" />
            Settings
            {settingsOpen ? (
              <ChevronUp className="size-3.5" />
            ) : (
              <ChevronDown className="size-3.5" />
            )}
          </button>

          <button
            type="button"
            className="flex items-center gap-1.5 text-xs font-medium text-muted-foreground hover:text-foreground transition-colors"
            onClick={() => setChatOpen((v) => !v)}
          >
            <MessageSquare className="size-3.5" />
            Chat
            {chatOpen ? (
              <ChevronUp className="size-3.5" />
            ) : (
              <ChevronDown className="size-3.5" />
            )}
          </button>
        </div>

        {/* ── Collapsible Settings ──────────────────────────────── */}
        {settingsOpen && (
          <div className="space-y-4 rounded-md border border-border p-3">
            {/* Stream Info (Title / Description) */}
            <div className="space-y-1.5">
              <Label className="text-xs font-medium flex items-center gap-1.5">
                <Tv className="size-3.5" />
                Stream Info
              </Label>
              <div className="grid grid-cols-2 gap-3">
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Name
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    value={name}
                    onChange={(e) => set(setName)(e.target.value)}
                  />
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Stream Title (Twitch/YouTube)
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    placeholder="e.g. 🎮 Interactive Chat Games!"
                    value={title}
                    onChange={(e) => set(setTitle)(e.target.value)}
                  />
                </div>
              </div>
              <div className="space-y-1.5">
                <Label className="text-[10px] text-muted-foreground">
                  Stream Description
                </Label>
                <Textarea
                  className="min-h-[60px] text-xs"
                  placeholder="Describe your stream for viewers…"
                  value={description}
                  onChange={(e) => set(setDescription)(e.target.value)}
                />
              </div>
            </div>

            {/* Resolution / Game Mode */}
            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1.5">
                <Label className="text-xs">Resolution</Label>
                <Select
                  value={resolution}
                  onValueChange={(v) =>
                    set(setResolution)(v as "mobile" | "desktop")
                  }
                >
                  <SelectTrigger className="h-8 text-xs">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="mobile">Mobile (1080×1920)</SelectItem>
                    <SelectItem value="desktop">Desktop (1920×1080)</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1.5">
                <Label className="text-xs">Game Mode</Label>
                <Select
                  value={gameMode}
                  onValueChange={(v) =>
                    set(setGameMode)(v as "fixed" | "vote" | "random")
                  }
                >
                  <SelectTrigger className="h-8 text-xs">
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

            {gameMode === "fixed" && (
              <div className="space-y-1.5">
                <Label className="text-xs">Fixed Game</Label>
                <Select
                  value={fixedGame}
                  onValueChange={set(setFixedGame)}
                >
                  <SelectTrigger className="h-8 text-xs">
                    <SelectValue placeholder="Select game…" />
                  </SelectTrigger>
                  <SelectContent>
                    {games.map((g) => (
                      <SelectItem key={g.id} value={g.id}>
                        {g.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            )}

            {/* Channels */}
            <div className="space-y-1.5">
              <Label className="text-xs">Chat Channels</Label>
              <div className="flex flex-wrap gap-1.5">
                {channels.map((ch) => (
                  <Badge
                    key={ch.id}
                    variant={
                      channelIds.includes(ch.id) ? "default" : "outline"
                    }
                    className="cursor-pointer text-[10px]"
                    onClick={() => toggleChannel(ch.id)}
                  >
                    {ch.name || ch.id}
                    {ch.connected && (
                      <span className="ml-1 inline-block size-1.5 rounded-full bg-green-500" />
                    )}
                  </Badge>
                ))}
                {channels.length === 0 && (
                  <span className="text-[10px] text-muted-foreground">
                    No channels configured — add them in the Channels tab
                  </span>
                )}
              </div>
            </div>

            {/* RTMP / Streaming Output */}
            <div className="space-y-1.5">
              <Label className="text-xs font-medium">
                RTMP Output
              </Label>
              <div className="grid grid-cols-2 gap-3">
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    RTMP URL
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    placeholder="rtmp://live.twitch.tv/app"
                    value={streamUrl}
                    onChange={(e) => set(setStreamUrl)(e.target.value)}
                  />
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Stream Key
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    type="password"
                    placeholder="live_..."
                    value={streamKey}
                    onChange={(e) => set(setStreamKey)(e.target.value)}
                  />
                </div>
              </div>
              {!hasValidUrl && (
                <p className="text-[10px] text-yellow-500">
                  ⚠ No RTMP URL set. Streaming cannot start without a valid URL.
                </p>
              )}
            </div>

            {/* Encoding Settings */}
            <div className="space-y-1.5">
              <Label className="text-xs font-medium">Encoding</Label>
              <div className="grid grid-cols-4 gap-3">
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    FPS
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    type="number"
                    min={1}
                    max={60}
                    value={fps}
                    onChange={(e) => set(setFps)(Number(e.target.value))}
                  />
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Bitrate (kbps)
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    type="number"
                    min={500}
                    max={20000}
                    step={500}
                    value={bitrate}
                    onChange={(e) => set(setBitrate)(Number(e.target.value))}
                  />
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Preset
                  </Label>
                  <Select value={preset} onValueChange={set(setPreset)}>
                    <SelectTrigger className="h-8 text-xs">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="ultrafast">ultrafast</SelectItem>
                      <SelectItem value="superfast">superfast</SelectItem>
                      <SelectItem value="veryfast">veryfast</SelectItem>
                      <SelectItem value="faster">faster</SelectItem>
                      <SelectItem value="fast">fast</SelectItem>
                      <SelectItem value="medium">medium</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Codec
                  </Label>
                  <Select value={codec} onValueChange={set(setCodec)}>
                    <SelectTrigger className="h-8 text-xs">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="libx264">libx264</SelectItem>
                      <SelectItem value="libx265">libx265</SelectItem>
                      <SelectItem value="h264_nvenc">h264_nvenc</SelectItem>
                      <SelectItem value="hevc_nvenc">hevc_nvenc</SelectItem>
                      <SelectItem value="h264_qsv">h264_qsv</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
              </div>
            </div>

            {/* Save button */}
            <Button
              size="sm"
              className="w-full gap-1"
              disabled={!dirty || saving}
              onClick={handleSave}
            >
              <Save className="size-3.5" />
              {saving ? "Saving…" : "Save Settings"}
            </Button>
          </div>
        )}

        {/* ── Collapsible Chat ──────────────────────────────────── */}
        {chatOpen && (
          <div className="space-y-2 rounded-md border border-border p-3">
            <Label className="text-xs font-medium flex items-center gap-1.5">
              <MessageSquare className="size-3.5" />
              Live Chat
            </Label>

            {/* Chat log */}
            <div className="h-[180px] overflow-auto rounded bg-muted/50 p-2 text-xs font-mono">
              {chatLog.length === 0 && (
                <p className="text-muted-foreground">No messages yet…</p>
              )}
              {chatLog.slice(-50).map((entry, i) => (
                <p key={i} className="leading-relaxed">
                  {entry}
                </p>
              ))}

            </div>

            {/* Target + User selector */}
            <div className="flex gap-2">
              <Select value={chatTarget} onValueChange={setChatTarget}>
                <SelectTrigger className="h-8 w-[140px] text-xs">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="local">🖥️ Local (Test)</SelectItem>
                  <SelectItem value="all">📡 All Channels</SelectItem>
                  {channels
                    .filter((ch) => ch.id !== "local")
                    .map((ch) => (
                      <SelectItem key={ch.id} value={ch.id}>
                        {ch.platform === "twitch" ? "🟣" : ch.platform === "youtube" ? "🔴" : "📡"}{" "}
                        {ch.name || ch.id}
                        {ch.connected ? "" : " (offline)"}
                      </SelectItem>
                    ))}
                </SelectContent>
              </Select>
              {chatTarget === "local" && (
                <Select value={chatUser} onValueChange={setChatUser}>
                  <SelectTrigger className="h-8 w-[100px] text-xs">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {["Player1", "Player2", "Player3", "Player4"].map((p) => (
                      <SelectItem key={p} value={p}>
                        {p}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              )}
            </div>

            {/* Chat input */}
            <div className="flex gap-2">
              <Input
                className="h-8 flex-1 text-xs"
                placeholder={chatTarget === "local" ? "!join, !attack, !vote …" : "Type a message…"}
                value={chatMsg}
                onChange={(e) => setChatMsg(e.target.value)}
                onKeyDown={(e) => e.key === "Enter" && handleChatSend()}
              />
              <Button
                size="sm"
                variant="secondary"
                className="h-8"
                onClick={handleChatSend}
              >
                <Send className="size-3.5" />
              </Button>
            </div>

            {/* Quick commands (only for local test) */}
            {chatTarget === "local" && (
              <div className="flex flex-wrap gap-1">
                {["!join", "!left", "!right", "!jump", "!jumpleft", "!jumpright", "!attack", "!special", "!dash", "!block"].map(
                  (cmd) => (
                    <Button
                      key={cmd}
                      size="sm"
                      variant="ghost"
                      className="h-6 px-2 text-[10px]"
                      onClick={() => handleQuickCmd(cmd)}
                    >
                      {cmd}
                    </Button>
                  )
                )}
              </div>
            )}
          </div>
        )}
      </CardContent>

      <CardFooter
        className="flex justify-between border-t pt-3"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex gap-1">
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                size="sm"
                variant={stream.streaming ? "destructive" : "default"}
                className="h-7 gap-1 text-xs"
                onClick={handleStartStop}
              >
                {stream.streaming ? (
                  <>
                    <Square className="size-3" /> Stop
                  </>
                ) : (
                  <>
                    <Play className="size-3" /> Start
                  </>
                )}
              </Button>
            </TooltipTrigger>
            <TooltipContent>
              {stream.streaming
                ? "Stop streaming"
                : hasValidUrl
                  ? "Start streaming"
                  : "Configure RTMP URL first"}
            </TooltipContent>
          </Tooltip>
        </div>
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              size="icon"
              variant="ghost"
              className="size-7 text-destructive hover:text-destructive"
              onClick={handleDelete}
            >
              <Trash2 className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>Delete Stream</TooltipContent>
        </Tooltip>
      </CardFooter>
    </Card>
  );
}
