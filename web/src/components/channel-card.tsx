"use client";

import { Plug, PlugZap, Trash2, Radio } from "lucide-react";
import type { ChannelState } from "@/lib/api";
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
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { toast } from "sonner";

interface ChannelCardProps {
  channel: ChannelState;
  onRefresh?: () => void;
}

const platformIcons: Record<string, string> = {
  local: "🖥️",
  twitch: "🟣",
  youtube: "🔴",
};

export function ChannelCard({ channel, onRefresh }: ChannelCardProps) {
  const handleConnect = async () => {
    try {
      if (channel.connected) {
        await api.disconnectChannel(channel.id);
        toast.success(`Disconnected from ${channel.name}`);
      } else {
        await api.connectChannel(channel.id);
        toast.success(`Connected to ${channel.name}`);
      }
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleDelete = async () => {
    if (channel.id === "local") {
      toast.error("Cannot delete the local channel");
      return;
    }
    if (!confirm(`Delete channel "${channel.name}"?`)) return;
    try {
      await api.deleteChannel(channel.id);
      toast.success("Channel deleted");
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  return (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
        <CardTitle className="flex items-center gap-2 text-sm font-medium">
          <span className="text-lg">{platformIcons[channel.platform] ?? "📡"}</span>
          {channel.name || channel.id}
        </CardTitle>
        <div className="flex items-center gap-1.5">
          <Badge variant="outline" className="text-[10px] capitalize">
            {channel.platform}
          </Badge>
          <Badge
            className={cn(
              "text-[10px]",
              channel.connected
                ? "bg-green-600 hover:bg-green-600"
                : "bg-muted text-muted-foreground hover:bg-muted"
            )}
          >
            {channel.connected ? (
              <>
                <Radio className="mr-1 size-3" />
                Connected
              </>
            ) : (
              "Disconnected"
            )}
          </Badge>
        </div>
      </CardHeader>

      <CardContent>
        <p className="text-xs text-muted-foreground">
          ID: <code className="rounded bg-muted px-1 py-0.5">{channel.id}</code>
          {channel.enabled !== undefined && (
            <span className="ml-3">
              Enabled:{" "}
              <span className={channel.enabled ? "text-green-500" : "text-red-500"}>
                {channel.enabled ? "Yes" : "No"}
              </span>
            </span>
          )}
        </p>
      </CardContent>

      <CardFooter className="flex justify-between border-t pt-3">
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              size="sm"
              variant={channel.connected ? "outline" : "default"}
              className="h-7 gap-1 text-xs"
              onClick={handleConnect}
            >
              {channel.connected ? (
                <>
                  <Plug className="size-3" /> Disconnect
                </>
              ) : (
                <>
                  <PlugZap className="size-3" /> Connect
                </>
              )}
            </Button>
          </TooltipTrigger>
          <TooltipContent>
            {channel.connected ? "Disconnect channel" : "Connect channel"}
          </TooltipContent>
        </Tooltip>

        {channel.id !== "local" && (
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
            <TooltipContent>Delete Channel</TooltipContent>
          </Tooltip>
        )}
      </CardFooter>
    </Card>
  );
}
