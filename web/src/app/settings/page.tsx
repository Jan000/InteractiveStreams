"use client";

import { useEffect, useState } from "react";
import { Save } from "lucide-react";
import { api } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
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
  const [port, setPort] = useState("8080");
  const [fps, setFps] = useState("30");
  const [headless, setHeadless] = useState(false);
  const [ffmpegPath, setFfmpegPath] = useState("ffmpeg");

  useEffect(() => {
    api
      .getSettings()
      .then((s) => {
        setSettings(s);
        // Extract known fields
        const web = s.web as Record<string, unknown> | undefined;
        const app = s.application as Record<string, unknown> | undefined;
        const streaming = s.streaming as Record<string, unknown> | undefined;
        if (web?.port) setPort(String(web.port));
        if (app?.fps) setFps(String(app.fps));
        if (app?.headless !== undefined) setHeadless(Boolean(app.headless));
        if (streaming?.ffmpeg_path) setFfmpegPath(String(streaming.ffmpeg_path));
      })
      .catch((e) => toast.error(e instanceof Error ? e.message : "Failed"))
      .finally(() => setLoading(false));
  }, []);

  const handleSave = async () => {
    setSaving(true);
    try {
      await api.updateSettings({
        web: { port: parseInt(port) || 8080 },
        application: {
          fps: parseInt(fps) || 30,
          headless,
        },
        streaming: {
          ffmpeg_path: ffmpegPath,
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
              <Input
                id="fps"
                type="number"
                min={1}
                max={120}
                value={fps}
                onChange={(e) => setFps(e.target.value)}
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
              <Input
                id="port"
                type="number"
                min={1024}
                max={65535}
                value={port}
                onChange={(e) => setPort(e.target.value)}
              />
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
