"use client";

import { useEffect, useRef, useState } from "react";
import { Save, ShieldCheck, Download, Upload } from "lucide-react";
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
  const [exporting, setExporting] = useState(false);
  const [importing, setImporting] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  // Editable fields
  const [port, setPort] = useState(8080);
  const [fps, setFps] = useState(30);
  const [headless, setHeadless] = useState(false);
  const [ffmpegPath, setFfmpegPath] = useState("ffmpeg");
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
      });
      await api.saveConfig();
      toast.success("Settings saved");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save settings");
    } finally {
      setSaving(false);
    }
  };

  const handleExport = async () => {
    setExporting(true);
    try {
      const data = await api.exportConfig();
      const blob = new Blob([JSON.stringify(data, null, 2)], {
        type: "application/json",
      });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `interactive_streams_config_${new Date().toISOString().slice(0, 10)}.json`;
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
      toast.success("Config exported");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Export failed");
    } finally {
      setExporting(false);
    }
  };

  const handleImport = (file: File) => {
    setImporting(true);
    const reader = new FileReader();
    reader.onload = async (ev) => {
      try {
        const data = JSON.parse(ev.target?.result as string);
        if (!data._export_version) {
          toast.error("Invalid config file — missing export version");
          return;
        }
        await api.importConfig(data);
        toast.success(
          "Config imported — the app will restart to apply changes…"
        );
        // Wait for the restart, then reload
        setTimeout(() => window.location.reload(), 5000);
      } catch (e) {
        toast.error(e instanceof Error ? e.message : "Import failed");
      } finally {
        setImporting(false);
        // Reset the file input so the same file can be re-selected
        if (fileInputRef.current) fileInputRef.current.value = "";
      }
    };
    reader.onerror = () => {
      toast.error("Failed to read file");
      setImporting(false);
    };
    reader.readAsText(file);
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

        {/* Export / Import */}
        <Card>
          <CardHeader>
            <CardTitle className="text-sm">Export / Import</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <p className="text-xs text-muted-foreground">
                Download all settings (channels, streams, profiles, audio) as a
                JSON file, or restore from a previously exported file.
              </p>
              <div className="flex gap-2">
                <Button
                  variant="outline"
                  size="sm"
                  className="gap-1"
                  onClick={handleExport}
                  disabled={exporting}
                >
                  <Download className="size-4" />
                  {exporting ? "Exporting…" : "Export"}
                </Button>
                <Button
                  variant="outline"
                  size="sm"
                  className="gap-1"
                  onClick={() => fileInputRef.current?.click()}
                  disabled={importing}
                >
                  <Upload className="size-4" />
                  {importing ? "Importing…" : "Import"}
                </Button>
                <input
                  ref={fileInputRef}
                  type="file"
                  accept=".json"
                  className="hidden"
                  onChange={(e) => {
                    const f = e.target.files?.[0];
                    if (f) handleImport(f);
                  }}
                />
              </div>
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
