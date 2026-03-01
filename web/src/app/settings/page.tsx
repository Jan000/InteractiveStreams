"use client";

import { useEffect, useState } from "react";
import { Save, ShieldCheck } from "lucide-react";
import { api } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { NumericInput } from "@/components/ui/numeric-input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { toast } from "sonner";

export default function SettingsPage() {
  const [settings, setSettings] = useState<Record<string, unknown> | null>(
    null
  );
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);

  // Editable fields
  const [port, setPort] = useState(8080);
  const [fps, setFps] = useState(30);
  const [headless, setHeadless] = useState(false);
  const [ffmpegPath, setFfmpegPath] = useState("ffmpeg");
  const [twitchClientId, setTwitchClientId] = useState("");
  const [twitchRedirectUri, setTwitchRedirectUri] = useState(
    "http://localhost:8080/auth/twitch/callback/"
  );
  // API key – stored in localStorage (client-side), sent to server which
  // validates it against its own web.api_key config.  Empty = no auth.
  const [apiKey, setApiKey] = useState("");

  useEffect(() => {
    // Load API key from localStorage
    const savedKey = localStorage.getItem("is_api_key") ?? "";
    setApiKey(savedKey);

    api
      .getSettings()
      .then((s) => {
        setSettings(s);
        // Extract known fields
        const web = s.web as Record<string, unknown> | undefined;
        const app = s.application as Record<string, unknown> | undefined;
        const streaming = s.streaming as Record<string, unknown> | undefined;
        if (web?.port) setPort(Number(web.port));
        if (app?.target_fps) setFps(Number(app.target_fps));
        if (app?.headless !== undefined) setHeadless(Boolean(app.headless));
        if (streaming?.ffmpeg_path) setFfmpegPath(String(streaming.ffmpeg_path));
        const twitch = s.twitch as Record<string, unknown> | undefined;
        if (twitch?.client_id) setTwitchClientId(String(twitch.client_id));
        if (twitch?.redirect_uri) setTwitchRedirectUri(String(twitch.redirect_uri));
      })
      .catch((e) => toast.error(e instanceof Error ? e.message : "Failed"))
      .finally(() => setLoading(false));
  }, []);

  const handleSave = async () => {
    setSaving(true);
    try {
      // Save API key to localStorage (used by api.ts for auth headers)
      if (apiKey) {
        localStorage.setItem("is_api_key", apiKey);
      } else {
        localStorage.removeItem("is_api_key");
      }

      await api.updateSettings({
        web: { port, api_key: apiKey },
        application: {
          target_fps: fps,
          headless,
        },
        streaming: {
          ffmpeg_path: ffmpegPath,
        },
        twitch: {
          client_id: twitchClientId,
          redirect_uri: twitchRedirectUri,
        },
      });
      await api.saveConfig();
      toast.success("Settings saved");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save settings");
    } finally {
      setSaving(false);
    }
  };

  if (loading) {
    return (
      <div className="space-y-4">
        <Skeleton className="h-8 w-48" />
        <Skeleton className="h-64" />
      </div>
    );
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold tracking-tight">Settings</h1>
          <p className="text-sm text-muted-foreground">
            Application configuration — saved to config/default.json
          </p>
        </div>
        <Button
          size="sm"
          className="gap-1"
          onClick={handleSave}
          disabled={saving}
        >
          <Save className="size-4" />
          {saving ? "Saving…" : "Save"}
        </Button>
      </div>

      <Separator />

      <div className="grid gap-6 md:grid-cols-2">
        {/* Application */}
        <Card>
          <CardHeader>
            <CardTitle className="text-sm">Application</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="fps">Target FPS</Label>
              <NumericInput
                id="fps"
                integer
                min={1}
                max={120}
                value={fps}
                onChange={setFps}
              />
            </div>
            <div className="flex items-center justify-between">
              <div className="space-y-0.5">
                <Label htmlFor="headless">Headless Mode</Label>
                <p className="text-xs text-muted-foreground">
                  Skip the SFML preview window (for servers)
                </p>
              </div>
              <Switch
                id="headless"
                checked={headless}
                onCheckedChange={setHeadless}
              />
            </div>
          </CardContent>
        </Card>

        {/* Web Server */}
        <Card>
          <CardHeader>
            <CardTitle className="text-sm">Web Server</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="port">Port</Label>
              <NumericInput
                id="port"
                integer
                min={1024}
                max={65535}
                value={port}
                onChange={setPort}
              />
            </div>
            <Separator />
            <div className="space-y-2">
              <div className="flex items-center gap-1.5">
                <ShieldCheck className="size-4 text-green-500" />
                <Label htmlFor="api-key">API Key</Label>
              </div>
              <Input
                id="api-key"
                type="password"
                value={apiKey}
                onChange={(e) => setApiKey(e.target.value)}
                placeholder="Leave empty to disable authentication"
              />
              <p className="text-xs text-muted-foreground">
                When set, all API requests must include this key as a Bearer
                token. The key is stored in your browser and sent to the server.
              </p>
            </div>
          </CardContent>
        </Card>

        {/* Streaming */}
        <Card>
          <CardHeader>
            <CardTitle className="text-sm">Streaming</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="ffmpeg">FFmpeg Path</Label>
              <Input
                id="ffmpeg"
                value={ffmpegPath}
                onChange={(e) => setFfmpegPath(e.target.value)}
                placeholder="ffmpeg"
              />
              <p className="text-xs text-muted-foreground">
                Path to ffmpeg binary (or just &quot;ffmpeg&quot; if in PATH)
              </p>
            </div>
          </CardContent>
        </Card>

        {/* Twitch Integration */}
        <Card>
          <CardHeader>
            <CardTitle className="text-sm">Twitch Integration</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="twitch-client-id">Client ID</Label>
              <Input
                id="twitch-client-id"
                value={twitchClientId}
                onChange={(e) => setTwitchClientId(e.target.value)}
                placeholder="Your Twitch Application Client ID"
              />
              <p className="text-xs text-muted-foreground">
                From{" "}
                <a
                  href="https://dev.twitch.tv/console/apps"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="underline hover:text-foreground"
                >
                  dev.twitch.tv/console
                </a>
                . Required for the &quot;Login with Twitch&quot; button on
                Channel cards.
              </p>
            </div>
            <div className="space-y-2">
              <Label htmlFor="twitch-redirect-uri">OAuth Redirect URI</Label>
              <Input
                id="twitch-redirect-uri"
                value={twitchRedirectUri}
                onChange={(e) => setTwitchRedirectUri(e.target.value)}
                placeholder="http://localhost:8080/auth/twitch/callback/"
              />
              <p className="text-xs text-muted-foreground">
                Must match the redirect URI registered in your Twitch
                application.
              </p>
            </div>
          </CardContent>
        </Card>

        {/* Raw Config Viewer */}
        <Card>
          <CardHeader>
            <CardTitle className="text-sm">Raw Config (read-only)</CardTitle>
          </CardHeader>
          <CardContent>
            <pre className="max-h-64 overflow-auto rounded-md bg-muted p-3 text-xs">
              {JSON.stringify(settings, null, 2)}
            </pre>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
