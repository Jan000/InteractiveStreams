"use client";

import {
  MonitorPlay,
  Play,
  Square,
  Trash2,
  RefreshCw,
  Radio,
} from "lucide-react";
import type { StreamState, GameInfo } from "@/lib/api";
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
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Tooltip,
  TooltipTrigger,
  TooltipContent,
} from "@/components/ui/tooltip";
import { toast } from "sonner";
import { useState } from "react";

interface StreamCardProps {
  stream: StreamState;
  games: GameInfo[];
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
  selected,
  onSelect,
  onRefresh,
}: StreamCardProps) {
  const [switchGame, setSwitchGame] = useState(stream.game?.id ?? "");
  const [switchMode, setSwitchMode] = useState("immediate");

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
        await api.startStreaming(stream.id);
        toast.success("Streaming started");
      }
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
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

  return (
    <Card
      className={cn(
        "cursor-pointer transition-colors",
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
        </CardTitle>
        <div className="flex items-center gap-1.5">
          <Badge variant="outline" className="text-[10px]">
            {modeLabels[stream.gameMode] ?? stream.gameMode}
          </Badge>
          <Badge variant="outline" className="text-[10px]">
            {resLabels[stream.resolution] ?? `${stream.width}×${stream.height}`}
          </Badge>
          {stream.streaming && (
            <Badge className="bg-red-600 text-[10px] hover:bg-red-600">
              <Radio className="mr-1 size-3" />
              LIVE
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
            <p className="font-medium">Vote active — {Math.ceil(stream.vote.timer)}s remaining</p>
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
              {stream.streaming ? "Stop streaming" : "Start streaming"}
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
