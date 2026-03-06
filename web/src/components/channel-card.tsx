"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import {
  Plug,
  PlugZap,
  Trash2,
  Radio,
  Save,
  Settings2,
  ChevronDown,
  ChevronUp,
  ExternalLink,
} from "lucide-react";
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
import { Input } from "@/components/ui/input";
import { NumericInput } from "@/components/ui/numeric-input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
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
  const isLocal = channel.id === "local";

  // Editable fields
  const [name, setName] = useState(channel.name ?? "");
  const [platform, setPlatform] = useState(channel.platform ?? "local");
  const [enabled, setEnabled] = useState(channel.enabled ?? false);

  // Platform-specific settings
  // Twitch
  const [twitchChannel, setTwitchChannel] = useState("");
  const [twitchOauth, setTwitchOauth] = useState("");
  const [twitchBot, setTwitchBot] = useState("InteractiveStreamsBot");
  const [twitchServer, setTwitchServer] = useState("irc.chat.twitch.tv");
  const [twitchPort, setTwitchPort] = useState(6667);
  const [twitchClientId, setTwitchClientId] = useState("");
  const [twitchRedirectUri, setTwitchRedirectUri] = useState("http://localhost:8080/auth/twitch/callback/");
  // Streaming output (Twitch / YouTube)
  const [streamUrl, setStreamUrl] = useState("");
  const [streamKey, setStreamKey] = useState("");
  // YouTube
  const [ytApiKey, setYtApiKey] = useState("");
  const [ytOauthToken, setYtOauthToken] = useState("");
  const [ytLiveChatId, setYtLiveChatId] = useState("");
  const [ytChannelId, setYtChannelId] = useState("");
  const [ytPollInterval, setYtPollInterval] = useState(2000);
  // Local
  const [consoleInput, setConsoleInput] = useState(true);

  // UI state
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [twitchAuthLoading, setTwitchAuthLoading] = useState(false);
  const oauthPopupRef = useRef<Window | null>(null);

  // Ref that always holds current form values — used by the async OAuth
  // callback so it never reads stale closure state.
  const formRef = useRef({
    twitchChannel, twitchBot, twitchServer, twitchPort,
    twitchClientId, twitchRedirectUri,
    streamUrl, streamKey, platform, name, enabled,
  });
  formRef.current = {
    twitchChannel, twitchBot, twitchServer, twitchPort,
    twitchClientId, twitchRedirectUri,
    streamUrl, streamKey, platform, name, enabled,
  };

  // Listen for OAuth token from popup callback page
  useEffect(() => {
    const handleMessage = async (event: MessageEvent) => {
      if (event.origin !== window.location.origin) return;
      if (event.data?.type !== "twitch-oauth") return;
      if (event.data.channelId !== channel.id) return;

      const token = event.data.accessToken as string;
      if (!token) return;

      setTwitchAuthLoading(false);
      setTwitchOauth(token);

      // Read form values via ref (avoids stale closures)
      const f = formRef.current;

      // Save ALL current settings with the new token, then auto-connect
      try {
        const settings = {
          channel: f.twitchChannel,
          oauth_token: token,
          bot_username: f.twitchBot,
          server: f.twitchServer,
          port: f.twitchPort,
          client_id: f.twitchClientId,
          redirect_uri: f.twitchRedirectUri,
          stream_url: f.streamUrl,
          stream_key: f.streamKey,
        };
        await api.updateChannel(channel.id, {
          platform: f.platform,
          name: f.name,
          enabled: f.enabled,
          settings,
        });
        await api.connectChannel(channel.id);
        toast.success("Twitch authenticated and connected!");
        setDirty(false);
        onRefresh?.();
      } catch {
        toast.error("Twitch token received — please save and connect manually.");
        setDirty(true);
      }
    };

    window.addEventListener("message", handleMessage);
    return () => window.removeEventListener("message", handleMessage);
  }, [channel.id, onRefresh]);

  const handleTwitchLogin = async () => {
    if (!twitchChannel.trim()) {
      toast.error("Please enter your Twitch channel name before logging in.");
      return;
    }
    setTwitchAuthLoading(true);
    try {
      const { url } = await api.getTwitchAuthUrl(channel.id);
      // Open popup centered on screen
      const w = 500, h = 700;
      const left = window.screenX + (window.outerWidth - w) / 2;
      const top = window.screenY + (window.outerHeight - h) / 2;
      oauthPopupRef.current = window.open(
        url,
        "twitch-oauth",
        `width=${w},height=${h},left=${left},top=${top},menubar=no,toolbar=no`
      );
      // Poll to detect if user closed popup without completing auth
      const timer = setInterval(() => {
        if (oauthPopupRef.current?.closed) {
          clearInterval(timer);
          setTwitchAuthLoading(false);
        }
      }, 500);
    } catch (e) {
      setTwitchAuthLoading(false);
      toast.error(e instanceof Error ? e.message : "Failed to start Twitch login");
    }
  };

  // Re-sync from server when channel data changes (polling)
  useEffect(() => {
    if (!dirty) {
      setName(channel.name ?? "");
      setPlatform(channel.platform ?? "local");
      setEnabled(channel.enabled ?? false);
      const s = channel.settings ?? {};
      // Twitch
      setTwitchChannel((s.channel as string) ?? "");
      setTwitchOauth((s.oauth_token as string) ?? "");
      setTwitchBot((s.bot_username as string) ?? "InteractiveStreamsBot");
      setTwitchServer((s.server as string) ?? "irc.chat.twitch.tv");
      setTwitchPort((s.port as number) ?? 6667);
      setTwitchClientId((s.client_id as string) ?? "");
      setTwitchRedirectUri((s.redirect_uri as string) ?? "http://localhost:8080/auth/twitch/callback/");
      // Streaming output
      setStreamUrl((s.stream_url as string) ?? "");
      setStreamKey((s.stream_key as string) ?? "");
      // YouTube
      setYtApiKey((s.api_key as string) ?? "");
      setYtOauthToken((s.oauth_token as string) ?? "");
      setYtLiveChatId((s.live_chat_id as string) ?? "");
      setYtChannelId((s.channel_id as string) ?? "");
      setYtPollInterval((s.poll_interval as number) ?? 2000);
      // Local
      setConsoleInput((s.console_input as boolean) ?? true);
    }
  }, [channel, dirty]);

  const markDirty = useCallback(
    <T,>(setter: React.Dispatch<React.SetStateAction<T>>) =>
      (val: T) => {
        setter(val);
        setDirty(true);
      },
    []
  );

  const buildSettings = (): Record<string, unknown> => {
    if (platform === "twitch") {
      return {
        channel: twitchChannel,
        oauth_token: twitchOauth,
        bot_username: twitchBot,
        server: twitchServer,
        port: twitchPort,
        client_id: twitchClientId,
        redirect_uri: twitchRedirectUri,
        stream_url: streamUrl,
        stream_key: streamKey,
      };
    }
    if (platform === "youtube") {
      return {
        api_key: ytApiKey,
        oauth_token: ytOauthToken,
        live_chat_id: ytLiveChatId,
        channel_id: ytChannelId,
        poll_interval: ytPollInterval,
        stream_url: streamUrl,
        stream_key: streamKey,
      };
    }
    return { console_input: consoleInput };
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      await api.updateChannel(channel.id, {
        platform,
        name,
        enabled,
        settings: buildSettings(),
      });
      toast.success("Channel updated");
      setDirty(false);
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to update");
    } finally {
      setSaving(false);
    }
  };

  const handleConnect = async () => {
    try {
      if (channel.connected) {
        await api.disconnectChannel(channel.id);
        toast.success(`Disconnected from ${channel.name}`);
      } else {
        // Auto-save unsaved changes before connecting so the server
        // has the latest settings (channel name, token, etc.)
        if (dirty) {
          await api.updateChannel(channel.id, {
            platform,
            name,
            enabled,
            settings: buildSettings(),
          });
          setDirty(false);
        }
        await api.connectChannel(channel.id);
        toast.success(`Connected to ${channel.name}`);
      }
      onRefresh?.();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleDelete = async () => {
    if (isLocal) {
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
          <span className="text-lg">
            {platformIcons[channel.platform] ?? "📡"}
          </span>
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
                ? channel.details?.waitingForLivestream
                  ? "bg-amber-600 hover:bg-amber-600"
                  : "bg-green-600 hover:bg-green-600"
                : "bg-muted text-muted-foreground hover:bg-muted"
            )}
          >
            {channel.connected ? (
              channel.details?.waitingForLivestream ? (
                <>
                  <Radio className="mr-1 size-3 animate-pulse" />
                  Waiting for livestream
                </>
              ) : (
                <>
                  <Radio className="mr-1 size-3" />
                  Connected
                </>
              )
            ) : (
              "Disconnected"
            )}
          </Badge>
          {dirty && (
            <Badge variant="secondary" className="text-[10px]">
              unsaved
            </Badge>
          )}
        </div>
      </CardHeader>

      <CardContent className="space-y-3">
        {/* Info row */}
        <p className="text-xs text-muted-foreground">
          ID:{" "}
          <code className="rounded bg-muted px-1 py-0.5">{channel.id}</code>
          <span className="ml-3">
            Enabled:{" "}
            <span
              className={enabled ? "text-green-500" : "text-red-500"}
            >
              {enabled ? "Yes" : "No"}
            </span>
          </span>
        </p>

        {/* Settings toggle */}
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

        {/* ── Collapsible Settings ────────────────────────────── */}
        {settingsOpen && (
          <div className="space-y-4 rounded-md border border-border p-3">
            {/* General */}
            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1.5">
                <Label className="text-xs">Name</Label>
                <Input
                  className="h-8 text-xs"
                  value={name}
                  onChange={(e) => markDirty(setName)(e.target.value)}
                  disabled={isLocal}
                />
              </div>
              <div className="space-y-1.5">
                <Label className="text-xs">Platform</Label>
                <Select
                  value={platform}
                  onValueChange={markDirty(setPlatform)}
                  disabled={isLocal}
                >
                  <SelectTrigger className="h-8 text-xs">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="local">Local</SelectItem>
                    <SelectItem value="twitch">Twitch</SelectItem>
                    <SelectItem value="youtube">YouTube</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>

            <div className="flex items-center gap-2">
              <Switch
                checked={enabled}
                onCheckedChange={markDirty(setEnabled)}
                disabled={isLocal}
              />
              <Label className="text-xs">Enabled</Label>
            </div>

            {/* ── Twitch settings ──────────────────────────────── */}
            {platform === "twitch" && (
              <div className="space-y-3">
                <Label className="text-xs font-medium">Twitch Settings</Label>
                <div className="grid grid-cols-2 gap-3">
                  <div className="space-y-1.5">
                    <Label className="text-[10px] text-muted-foreground">
                      Channel
                    </Label>
                    <Input
                      className="h-8 text-xs"
                      placeholder="your_channel"
                      value={twitchChannel}
                      onChange={(e) =>
                        markDirty(setTwitchChannel)(e.target.value)
                      }
                    />
                  </div>
                  <div className="space-y-1.5">
                    <Label className="text-[10px] text-muted-foreground">
                      Bot Username
                    </Label>
                    <Input
                      className="h-8 text-xs"
                      value={twitchBot}
                      onChange={(e) => markDirty(setTwitchBot)(e.target.value)}
                    />
                  </div>
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Client ID
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    placeholder="Your Twitch Application Client ID"
                    value={twitchClientId}
                    onChange={(e) =>
                      markDirty(setTwitchClientId)(e.target.value)
                    }
                  />
                  <p className="text-[9px] text-muted-foreground">
                    From{" "}
                    <a
                      href="https://dev.twitch.tv/console/apps"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="underline hover:text-foreground"
                    >
                      dev.twitch.tv/console
                    </a>
                    . Required for &quot;Login with Twitch&quot;.
                  </p>
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    OAuth Redirect URI
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    value={twitchRedirectUri}
                    onChange={(e) =>
                      markDirty(setTwitchRedirectUri)(e.target.value)
                    }
                    placeholder="http://localhost:8080/auth/twitch/callback/"
                  />
                  <p className="text-[9px] text-muted-foreground">
                    Must match the redirect URI in your Twitch app settings.
                  </p>
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    OAuth Token
                  </Label>
                  <div className="flex gap-2">
                    <Input
                      className="h-8 text-xs flex-1"
                      type="password"
                      placeholder="oauth:abc123..."
                      value={twitchOauth}
                      onChange={(e) =>
                        markDirty(setTwitchOauth)(e.target.value)
                      }
                    />
                    <Tooltip>
                      <TooltipTrigger asChild>
                        <Button
                          variant="outline"
                          size="sm"
                          className="h-8 gap-1.5 whitespace-nowrap bg-purple-600/20 border-purple-600/40 text-purple-300 hover:bg-purple-600/30 hover:text-purple-200"
                          onClick={handleTwitchLogin}
                          disabled={twitchAuthLoading}
                        >
                          {twitchAuthLoading ? (
                            <div className="size-3.5 animate-spin rounded-full border-2 border-purple-400/30 border-t-purple-400" />
                          ) : (
                            <ExternalLink className="size-3.5" />
                          )}
                          Login with Twitch
                        </Button>
                      </TooltipTrigger>
                      <TooltipContent>
                        <p>Open Twitch authorization (requires Client ID above)</p>
                      </TooltipContent>
                    </Tooltip>
                  </div>
                </div>
                <div className="grid grid-cols-2 gap-3">
                  <div className="space-y-1.5">
                    <Label className="text-[10px] text-muted-foreground">
                      IRC Server
                    </Label>
                    <Input
                      className="h-8 text-xs"
                      value={twitchServer}
                      onChange={(e) =>
                        markDirty(setTwitchServer)(e.target.value)
                      }
                    />
                  </div>
                  <div className="space-y-1.5">
                    <Label className="text-[10px] text-muted-foreground">
                      Port
                    </Label>
                    <NumericInput
                      className="h-8 text-xs"
                      integer
                      min={1}
                      max={65535}
                      value={twitchPort}
                      onChange={markDirty(setTwitchPort)}
                    />
                  </div>
                </div>
                {/* RTMP Streaming Output */}
                <div className="space-y-1.5 rounded border border-dashed p-2">
                  <Label className="text-[10px] font-medium text-muted-foreground">
                    Streaming Output (RTMP)
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
                        onChange={(e) =>
                          markDirty(setStreamUrl)(e.target.value)
                        }
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
                        onChange={(e) =>
                          markDirty(setStreamKey)(e.target.value)
                        }
                      />
                    </div>
                  </div>
                </div>
              </div>
            )}

            {/* ── YouTube settings ─────────────────────────────── */}
            {platform === "youtube" && (
              <div className="space-y-3">
                <Label className="text-xs font-medium">YouTube Settings</Label>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    API Key{" "}
                    <span className="text-muted-foreground/60">(read-only chat)</span>
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    type="password"
                    placeholder="AIza..."
                    value={ytApiKey}
                    onChange={(e) => markDirty(setYtApiKey)(e.target.value)}
                  />
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    OAuth Token{" "}
                    <span className="text-muted-foreground/60">(for title/description updates)</span>
                  </Label>
                  <Input
                    className="h-8 text-xs"
                    type="password"
                    placeholder="ya29.a0..."
                    value={ytOauthToken}
                    onChange={(e) => markDirty(setYtOauthToken)(e.target.value)}
                  />
                  <p className="text-[9px] text-muted-foreground">
                    Required for updating stream title &amp; description via YouTube Data API v3.
                    Needs youtube.force-ssl scope.
                    {!!(channel.details as Record<string, unknown>)?.hasOauthToken && (
                      <span className="ml-1 text-green-500">✓ Token set</span>
                    )}
                  </p>
                </div>
                <div className="grid grid-cols-2 gap-3">
                  <div className="space-y-1.5">
                    <Label className="text-[10px] text-muted-foreground">
                      Channel ID
                    </Label>
                    <Input
                      className="h-8 text-xs"
                      placeholder="UC..."
                      value={ytChannelId}
                      onChange={(e) =>
                        markDirty(setYtChannelId)(e.target.value)
                      }
                    />
                  </div>
                  <div className="space-y-1.5">
                    <Label className="text-[10px] text-muted-foreground">
                      Live Chat ID{" "}
                      <span className="text-muted-foreground/60">(optional)</span>
                    </Label>
                    <Input
                      className="h-8 text-xs"
                      placeholder="auto-detect from live stream"
                      value={ytLiveChatId}
                      onChange={(e) =>
                        markDirty(setYtLiveChatId)(e.target.value)
                      }
                    />
                    <p className="text-[9px] text-muted-foreground">
                      Leave empty to auto-detect from the active livestream.
                      {channel.connected && !!channel.details?.autoDetectedChatId && (
                        <span className="ml-1 text-green-500">
                          ✓ Auto-detected
                        </span>
                      )}
                    </p>
                  </div>
                </div>
                <div className="space-y-1.5">
                  <Label className="text-[10px] text-muted-foreground">
                    Poll Interval (ms)
                  </Label>
                  <NumericInput
                    className="h-8 text-xs"
                    integer
                    min={500}
                    max={30000}
                    step={500}
                    value={ytPollInterval}
                    onChange={markDirty(setYtPollInterval)}
                  />
                </div>
                {/* RTMP Streaming Output */}
                <div className="space-y-1.5 rounded border border-dashed p-2">
                  <Label className="text-[10px] font-medium text-muted-foreground">
                    Streaming Output (RTMP)
                  </Label>
                  <div className="grid grid-cols-2 gap-3">
                    <div className="space-y-1.5">
                      <Label className="text-[10px] text-muted-foreground">
                        RTMP URL
                      </Label>
                      <Input
                        className="h-8 text-xs"
                        placeholder="rtmp://a.rtmp.youtube.com/live2"
                        value={streamUrl}
                        onChange={(e) =>
                          markDirty(setStreamUrl)(e.target.value)
                        }
                      />
                    </div>
                    <div className="space-y-1.5">
                      <Label className="text-[10px] text-muted-foreground">
                        Stream Key
                      </Label>
                      <Input
                        className="h-8 text-xs"
                        type="password"
                        placeholder="xxxx-xxxx-xxxx-xxxx"
                        value={streamKey}
                        onChange={(e) =>
                          markDirty(setStreamKey)(e.target.value)
                        }
                      />
                    </div>
                  </div>
                </div>
              </div>
            )}

            {/* ── Local settings ───────────────────────────────── */}
            {platform === "local" && (
              <div className="space-y-3">
                <Label className="text-xs font-medium">Local Settings</Label>
                <div className="flex items-center gap-2">
                  <Switch
                    checked={consoleInput}
                    onCheckedChange={markDirty(setConsoleInput)}
                  />
                  <Label className="text-xs">Console Input</Label>
                </div>
              </div>
            )}

            {/* Save */}
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

        {!isLocal && (
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
