"use client";

import { useState } from "react";
import { Plus } from "lucide-react";
import { useStatus } from "@/hooks/use-status";
import { api } from "@/lib/api";
import { ChannelCard } from "@/components/channel-card";
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

export default function ChannelsPage() {
  const { data, error } = useStatus(2000);
  const [dialogOpen, setDialogOpen] = useState(false);

  // New channel form
  const [platform, setPlatform] = useState("twitch");
  const [name, setName] = useState("");
  const [twitchChannel, setTwitchChannel] = useState("");
  const [twitchToken, setTwitchToken] = useState("");
  const [ytVideoId, setYtVideoId] = useState("");
  const [ytApiKey, setYtApiKey] = useState("");

  const handleCreateChannel = async () => {
    try {
      const settings: Record<string, unknown> = {};
      let channelName = name;

      if (platform === "twitch") {
        settings.channel = twitchChannel;
        if (twitchToken) settings.oauth_token = twitchToken;
        channelName = channelName || twitchChannel;
      } else if (platform === "youtube") {
        settings.video_id = ytVideoId;
        if (ytApiKey) settings.api_key = ytApiKey;
        channelName = channelName || `YT-${ytVideoId}`;
      }

      await api.createChannel({
        platform,
        name: channelName,
        enabled: true,
        settings,
      });
      toast.success("Channel created");
      setDialogOpen(false);
      // Reset form
      setName("");
      setTwitchChannel("");
      setTwitchToken("");
      setYtVideoId("");
      setYtApiKey("");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to create channel");
    }
  };

  if (!data && !error) {
    return (
      <div className="space-y-4">
        <Skeleton className="h-8 w-48" />
        <div className="grid gap-4 md:grid-cols-2">
          {[1, 2, 3].map((i) => (
            <Skeleton key={i} className="h-36" />
          ))}
        </div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="flex flex-col items-center justify-center gap-4 py-20">
        <p className="text-sm text-destructive">Connection lost: {error}</p>
      </div>
    );
  }

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold tracking-tight">Channels</h1>
          <p className="text-sm text-muted-foreground">
            Manage chat platform connections (Twitch, YouTube, Local)
          </p>
        </div>
        <Dialog open={dialogOpen} onOpenChange={setDialogOpen}>
          <DialogTrigger asChild>
            <Button size="sm" className="gap-1">
              <Plus className="size-4" />
              Add Channel
            </Button>
          </DialogTrigger>
          <DialogContent>
            <DialogHeader>
              <DialogTitle>Add Channel</DialogTitle>
            </DialogHeader>
            <div className="space-y-4 pt-2">
              <div className="space-y-2">
                <Label>Platform</Label>
                <Select value={platform} onValueChange={setPlatform}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="twitch">Twitch</SelectItem>
                    <SelectItem value="youtube">YouTube</SelectItem>
                    <SelectItem value="local">Local</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>Display Name (optional)</Label>
                <Input
                  placeholder="e.g. My Twitch Channel"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                />
              </div>

              {platform === "twitch" && (
                <>
                  <div className="space-y-2">
                    <Label>Channel Name</Label>
                    <Input
                      placeholder="e.g. my_channel"
                      value={twitchChannel}
                      onChange={(e) => setTwitchChannel(e.target.value)}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label>OAuth Token (optional)</Label>
                    <Input
                      type="password"
                      placeholder="oauth:..."
                      value={twitchToken}
                      onChange={(e) => setTwitchToken(e.target.value)}
                    />
                  </div>
                </>
              )}

              {platform === "youtube" && (
                <>
                  <div className="space-y-2">
                    <Label>Video / Live ID</Label>
                    <Input
                      placeholder="e.g. dQw4w9WgXcQ"
                      value={ytVideoId}
                      onChange={(e) => setYtVideoId(e.target.value)}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label>API Key (optional)</Label>
                    <Input
                      type="password"
                      placeholder="AIza..."
                      value={ytApiKey}
                      onChange={(e) => setYtApiKey(e.target.value)}
                    />
                  </div>
                </>
              )}

              <Button className="w-full" onClick={handleCreateChannel}>
                Create Channel
              </Button>
            </div>
          </DialogContent>
        </Dialog>
      </div>

      <Separator />

      {/* Channel grid */}
      <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
        {data?.channels.map((ch) => (
          <ChannelCard key={ch.id} channel={ch} />
        ))}
      </div>

      {data?.channels.length === 0 && (
        <p className="py-12 text-center text-sm text-muted-foreground">
          No channels configured.
        </p>
      )}
    </div>
  );
}
