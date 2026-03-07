"use client";

import { useEffect, useState, useCallback } from "react";
import {
  Layers,
  Plus,
  Trash2,
  ChevronDown,
  GitBranch,
} from "lucide-react";
import { api, StreamProfile, GameInfo } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { NumericInput } from "@/components/ui/numeric-input";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { Badge } from "@/components/ui/badge";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { toast } from "sonner";

/** Scalar fields that a profile can override. */
const CONFIG_FIELDS = [
  // Stream Info
  { key: "title", label: "Stream Title", type: "text", group: "Stream Info" },
  { key: "description", label: "Stream Description", type: "text", group: "Stream Info" },
  // Core
  { key: "resolution", label: "Resolution", type: "select", options: ["mobile", "desktop"], group: "Core" },
  { key: "game_mode", label: "Game Mode", type: "select", options: ["fixed", "vote", "random"], group: "Core" },
  { key: "fixed_game", label: "Fixed Game", type: "text", group: "Core" },
  // Encoding
  { key: "fps", label: "FPS", type: "number", min: 1, max: 120, step: 1, integer: true, group: "Encoding" },
  { key: "bitrate_kbps", label: "Bitrate (kbps)", type: "number", min: 500, max: 20000, step: 100, integer: true, group: "Encoding" },
  { key: "preset", label: "Encoder Preset", type: "select", options: ["ultrafast", "superfast", "veryfast", "faster", "fast", "medium"], group: "Encoding" },
  { key: "codec", label: "Codec", type: "select", options: ["libx264", "libx265", "h264_nvenc", "hevc_nvenc", "h264_qsv"], group: "Encoding" },
  { key: "profile", label: "H.264 Profile", type: "select", options: ["baseline", "main", "high"], group: "Encoding" },
  { key: "tune", label: "Tune", type: "select", options: ["zerolatency", "film", "animation", "grain", "stillimage", "fastdecode"], group: "Encoding" },
  { key: "keyframe_interval", label: "Keyframe Interval (s)", type: "number", min: 1, max: 10, step: 1, integer: true, group: "Encoding" },
  { key: "threads", label: "Threads (0=auto)", type: "number", min: 0, max: 32, step: 1, integer: true, group: "Encoding" },
  { key: "maxrate_factor", label: "Maxrate Factor", type: "number", min: 1.0, max: 3.0, step: 0.1, group: "Encoding" },
  { key: "bufsize_factor", label: "Bufsize Factor", type: "number", min: 0.5, max: 3.0, step: 0.1, group: "Encoding" },
  { key: "audio_bitrate", label: "Audio Bitrate (kbps)", type: "number", min: 32, max: 320, step: 32, integer: true, group: "Audio" },
  { key: "audio_sample_rate", label: "Audio Sample Rate", type: "select", options: ["44100", "48000"], group: "Audio" },
  { key: "audio_codec", label: "Audio Codec", type: "select", options: ["aac", "libmp3lame", "libopus"], group: "Audio" },
  // Scoreboard
  { key: "scoreboard_top_n", label: "Scoreboard Top N", type: "number", min: 1, max: 20, step: 1, integer: true, group: "Scoreboard" },
  { key: "scoreboard_font_size", label: "Scoreboard Font Size", type: "number", min: 8, max: 72, step: 1, integer: true, group: "Scoreboard" },
  { key: "scoreboard_alltime_title", label: "All-Time Title", type: "text", group: "Scoreboard" },
  { key: "scoreboard_recent_title", label: "Recent Title", type: "text", group: "Scoreboard" },
  { key: "scoreboard_recent_hours", label: "Recent Hours", type: "number", min: 1, max: 168, step: 1, integer: true, group: "Scoreboard" },
  // Overlay
  { key: "vote_overlay_font_scale", label: "Vote Overlay Font Scale", type: "number", min: 0.1, max: 3, step: 0.1, group: "Overlay" },
] as const;

/** Per-game map fields that a profile can override. */
const PERGAME_FIELDS = [
  { key: "game_descriptions", label: "Stream Description", placeholder: "Description shown on platform…" },
  { key: "game_info_messages", label: "Info Message", placeholder: "Periodic chat info message…" },
  { key: "game_info_intervals", label: "Info Interval (s)", type: "number" as const },
  { key: "game_font_scales", label: "Font Scale", type: "number" as const },
  { key: "game_player_limits", label: "Max Players", type: "number" as const },
  { key: "game_twitch_categories", label: "Twitch Category", placeholder: "e.g. Just Chatting" },
  { key: "game_twitch_titles", label: "Twitch Title", placeholder: "Twitch stream title…" },
  { key: "game_youtube_titles", label: "YouTube Title", placeholder: "YouTube stream title…" },
] as const;

const SCALAR_DEFAULTS: Record<string, unknown> = {
  title: "",
  description: "",
  resolution: "mobile",
  game_mode: "fixed",
  fixed_game: "chaos_arena",
  fps: 30,
  bitrate_kbps: 6000,
  preset: "ultrafast",
  codec: "libx264",
  profile: "baseline",
  tune: "zerolatency",
  keyframe_interval: 2,
  threads: 0,
  maxrate_factor: 1.2,
  bufsize_factor: 1.0,
  audio_bitrate: 128,
  audio_sample_rate: 44100,
  audio_codec: "aac",
  scoreboard_top_n: 5,
  scoreboard_font_size: 20,
  scoreboard_alltime_title: "ALL TIME",
  scoreboard_recent_title: "LAST 24H",
  scoreboard_recent_hours: 24,
  vote_overlay_font_scale: 1.0,
};

interface ProfileEditorProps {
  profile: StreamProfile;
  allProfiles: StreamProfile[];
  games: GameInfo[];
  onSave: (updated: StreamProfile) => Promise<void>;
  onDelete: (id: string) => Promise<void>;
}

function ProfileEditor({ profile, allProfiles, games, onSave, onDelete }: ProfileEditorProps) {
  const [name, setName] = useState(profile.name);
  const [parentId, setParentId] = useState(profile.parent_id);
  const [config, setConfig] = useState<Record<string, unknown>>(profile.config ?? {});
  const [saving, setSaving] = useState(false);
  const [perGameOpen, setPerGameOpen] = useState(false);

  // Reset when profile changes
  useEffect(() => {
    setName(profile.name);
    setParentId(profile.parent_id);
    setConfig(profile.config ?? {});
  }, [profile]);

  const availableParents = allProfiles.filter((p) => p.id !== profile.id);

  const setField = (key: string, value: unknown) => {
    setConfig((prev) => ({ ...prev, [key]: value }));
  };

  const removeField = (key: string) => {
    setConfig((prev) => {
      const next = { ...prev };
      delete next[key];
      return next;
    });
  };

  // Per-game map helpers
  const getMap = (key: string): Record<string, unknown> =>
    (config[key] as Record<string, unknown>) ?? {};

  const setMapEntry = (mapKey: string, gameId: string, value: unknown) => {
    setConfig((prev) => {
      const map = { ...((prev[mapKey] as Record<string, unknown>) ?? {}) };
      if (value === "" || value === 0 || value === undefined) {
        delete map[gameId];
      } else {
        map[gameId] = value;
      }
      const next = { ...prev };
      if (Object.keys(map).length > 0) {
        next[mapKey] = map;
      } else {
        delete next[mapKey];
      }
      return next;
    });
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      // Clean undefined values from config before saving
      const cleanConfig: Record<string, unknown> = {};
      for (const [k, v] of Object.entries(config)) {
        if (v !== undefined) cleanConfig[k] = v;
      }
      await onSave({ ...profile, name, parent_id: parentId, config: cleanConfig });
      toast.success(`Profile "${name}" saved`);
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save");
    } finally {
      setSaving(false);
    }
  };

  // Group fields
  const groups = CONFIG_FIELDS.reduce<Record<string, typeof CONFIG_FIELDS[number][]>>((acc, f) => {
    const g = f.group;
    if (!acc[g]) acc[g] = [];
    acc[g].push(f);
    return acc;
  }, {});

  return (
    <Card>
      <CardHeader className="pb-3">
        <div className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2 text-base">
            <Layers className="size-4" />
            {profile.name || "Unnamed Profile"}
          </CardTitle>
          <div className="flex gap-2">
            <Button size="sm" onClick={handleSave} disabled={saving}>
              {saving ? "Saving…" : "Save"}
            </Button>
            <Button
              size="sm"
              variant="destructive"
              onClick={() => onDelete(profile.id)}
            >
              <Trash2 className="size-3.5" />
            </Button>
          </div>
        </div>
      </CardHeader>
      <CardContent className="space-y-4">
        {/* Name */}
        <div className="space-y-1">
          <Label>Profile Name</Label>
          <Input value={name} onChange={(e) => setName(e.target.value)} />
        </div>

        {/* Parent */}
        <div className="space-y-1">
          <Label className="flex items-center gap-1">
            <GitBranch className="size-3.5" /> Inherits From
          </Label>
          <Select value={parentId || "__none__"} onValueChange={(v) => setParentId(v === "__none__" ? "" : v)}>
            <SelectTrigger>
              <SelectValue placeholder="No parent" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="__none__">No parent (standalone)</SelectItem>
              {availableParents.map((p) => (
                <SelectItem key={p.id} value={p.id}>
                  {p.name}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          {parentId && (
            <p className="text-xs text-muted-foreground">
              Fields not set here will be inherited from the parent profile.
            </p>
          )}
        </div>

        <Separator />

        {/* Scalar config fields, grouped */}
        <div className="space-y-4">
          <Label className="text-sm font-semibold">Configuration Defaults</Label>
          <p className="text-xs text-muted-foreground">
            Only set fields you want to override. Unset fields will use the
            parent profile&apos;s values (or system defaults).
          </p>

          {Object.entries(groups).map(([groupName, fields]) => (
            <div key={groupName} className="space-y-2">
              <span className="text-xs font-medium text-muted-foreground uppercase tracking-wider">
                {groupName}
              </span>
              {fields.map((field) => {
                const isSet = field.key in config;
                const val = config[field.key];

                return (
                  <div key={field.key} className="flex items-center gap-3">
                    <div className="w-44 shrink-0">
                      <span className="text-sm">{field.label}</span>
                    </div>
                    <div className="flex-1">
                      {isSet ? (
                        field.type === "select" ? (
                          <Select
                            value={String(val ?? "")}
                            onValueChange={(v) => setField(field.key, v)}
                          >
                            <SelectTrigger className="h-8">
                              <SelectValue />
                            </SelectTrigger>
                            <SelectContent>
                              {"options" in field && field.options.map((o) => (
                                <SelectItem key={o} value={o}>
                                  {o}
                                </SelectItem>
                              ))}
                            </SelectContent>
                          </Select>
                        ) : field.type === "number" ? (
                          <NumericInput
                            value={Number(val ?? 0)}
                            onChange={(v) => setField(field.key, v)}
                            min={"min" in field ? field.min : undefined}
                            max={"max" in field ? field.max : undefined}
                            step={"step" in field ? field.step : undefined}
                            integer={"integer" in field && field.integer}
                            className="h-8"
                          />
                        ) : (
                          <Input
                            value={String(val ?? "")}
                            onChange={(e) => setField(field.key, e.target.value)}
                            className="h-8"
                          />
                        )
                      ) : (
                        <span className="text-xs text-muted-foreground italic">
                          inherited
                        </span>
                      )}
                    </div>
                    <div className="w-20 shrink-0">
                      {isSet ? (
                        <Button
                          variant="ghost"
                          size="sm"
                          className="h-7 text-xs"
                          onClick={() => removeField(field.key)}
                        >
                          Unset
                        </Button>
                      ) : (
                        <Button
                          variant="outline"
                          size="sm"
                          className="h-7 text-xs"
                          onClick={() => {
                            setField(field.key, SCALAR_DEFAULTS[field.key] ?? "");
                          }}
                        >
                          Set
                        </Button>
                      )}
                    </div>
                  </div>
                );
              })}
            </div>
          ))}
        </div>

        <Separator />

        {/* Per-game map settings */}
        <div className="space-y-3">
          <button
            type="button"
            className="flex items-center gap-1.5 text-sm font-semibold hover:text-foreground transition-colors"
            onClick={() => setPerGameOpen((v) => !v)}
          >
            Per-Game Settings
            <ChevronDown className={`size-4 transition-transform ${perGameOpen ? "rotate-180" : ""}`} />
          </button>

          {perGameOpen && (
            <div className="space-y-3">
              <p className="text-xs text-muted-foreground">
                Set per-game overrides for descriptions, info messages, font scales, player limits, and platform titles.
              </p>
              {games.map((g) => (
                <div key={g.id} className="space-y-1.5 rounded border p-2">
                  <span className="text-xs font-semibold">{g.name}</span>
                  {PERGAME_FIELDS.map((field) => {
                    const map = getMap(field.key);
                    const val = map[g.id];

                    return (
                      <div key={field.key} className="flex items-center gap-2">
                        <Label className="text-[10px] w-28 shrink-0 text-muted-foreground">
                          {field.label}
                        </Label>
                        {"type" in field && field.type === "number" ? (
                          <NumericInput
                            className="h-7 flex-1 text-xs"
                            value={Number(val ?? 0)}
                            min={0}
                            step={field.key === "game_font_scales" ? 0.1 : 1}
                            integer={field.key !== "game_font_scales"}
                            onChange={(v) => setMapEntry(field.key, g.id, v)}
                            placeholder="0"
                          />
                        ) : (
                          <Input
                            className="h-7 flex-1 text-xs"
                            placeholder={"placeholder" in field ? field.placeholder : ""}
                            value={String(val ?? "")}
                            onChange={(e) => setMapEntry(field.key, g.id, e.target.value)}
                          />
                        )}
                      </div>
                    );
                  })}
                </div>
              ))}
            </div>
          )}
        </div>
      </CardContent>
    </Card>
  );
}

export default function ProfilesPage() {
  const [profiles, setProfiles] = useState<StreamProfile[]>([]);
  const [games, setGames] = useState<GameInfo[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchData = useCallback(async () => {
    try {
      const [p, g] = await Promise.all([api.getProfiles(), api.getGames()]);
      setProfiles(p);
      setGames(g);
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to load data");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const handleCreate = async () => {
    try {
      await api.createProfile({
        name: "New Profile",
        parent_id: "",
        config: {},
      });
      await fetchData();
      toast.success("Profile created");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to create profile");
    }
  };

  const handleSave = async (updated: StreamProfile) => {
    await api.updateProfile(updated.id, updated);
    await fetchData();
  };

  const handleDelete = async (id: string) => {
    if (!confirm("Delete this profile? Streams using it will lose their parent."))
      return;
    await api.deleteProfile(id);
    await fetchData();
    toast.success("Profile deleted");
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
          <h1 className="text-2xl font-bold tracking-tight">Profiles</h1>
          <p className="text-sm text-muted-foreground">
            Global configuration profiles with inheritance. Streams can inherit
            defaults from a profile. Changes to a profile propagate to all
            streams that reference it.
          </p>
        </div>
        <Button onClick={handleCreate}>
          <Plus className="mr-2 size-4" />
          New Profile
        </Button>
      </div>

      {profiles.length === 0 ? (
        <Card>
          <CardContent className="flex flex-col items-center justify-center py-12 text-center">
            <Layers className="mb-4 size-10 text-muted-foreground" />
            <p className="text-sm text-muted-foreground">
              No profiles yet. Create one to define reusable stream
              configuration defaults.
            </p>
          </CardContent>
        </Card>
      ) : (
        <div className="space-y-4">
          {/* Inheritance visualization */}
          {profiles.some((p) => p.parent_id) && (
            <Card>
              <CardHeader className="pb-2">
                <CardTitle className="text-sm">Inheritance Chain</CardTitle>
              </CardHeader>
              <CardContent>
                <div className="flex flex-wrap gap-2">
                  {profiles.map((p) => {
                    const parent = profiles.find(
                      (pp) => pp.id === p.parent_id
                    );
                    return (
                      <Badge key={p.id} variant="outline" className="gap-1">
                        {parent && (
                          <>
                            <span className="text-muted-foreground">
                              {parent.name}
                            </span>
                            <ChevronDown className="size-3 rotate-[-90deg]" />
                          </>
                        )}
                        {p.name}
                      </Badge>
                    );
                  })}
                </div>
              </CardContent>
            </Card>
          )}

          {/* Profile editors */}
          {profiles.map((p) => (
            <ProfileEditor
              key={p.id}
              profile={p}
              allProfiles={profiles}
              games={games}
              onSave={handleSave}
              onDelete={handleDelete}
            />
          ))}
        </div>
      )}
    </div>
  );
}
