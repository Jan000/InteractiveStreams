"use client";

import { useEffect, useState, useCallback } from "react";
import {
  Layers,
  Plus,
  Trash2,
  ChevronDown,
  GitBranch,
} from "lucide-react";
import { api, StreamProfile } from "@/lib/api";
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

/** Fields that make sense to set in a profile (subset of StreamConfig). */
const CONFIG_FIELDS = [
  { key: "resolution", label: "Resolution", type: "select", options: ["mobile", "desktop"] },
  { key: "game_mode", label: "Game Mode", type: "select", options: ["fixed", "vote", "random"] },
  { key: "fixed_game", label: "Fixed Game", type: "text" },
  { key: "fps", label: "FPS", type: "number", min: 1, max: 120, step: 1, integer: true },
  { key: "bitrate_kbps", label: "Bitrate (kbps)", type: "number", min: 500, max: 20000, step: 100, integer: true },
  { key: "preset", label: "Encoder Preset", type: "text" },
  { key: "codec", label: "Codec", type: "text" },
  { key: "scoreboard_top_n", label: "Scoreboard Top N", type: "number", min: 1, max: 20, step: 1, integer: true },
  { key: "scoreboard_font_size", label: "Scoreboard Font Size", type: "number", min: 8, max: 72, step: 1, integer: true },
  { key: "scoreboard_recent_hours", label: "Scoreboard Recent Hours", type: "number", min: 1, max: 168, step: 1, integer: true },
  { key: "vote_overlay_font_scale", label: "Vote Overlay Font Scale", type: "number", min: 0.1, max: 3, step: 0.1 },
] as const;

interface ProfileEditorProps {
  profile: StreamProfile;
  allProfiles: StreamProfile[];
  onSave: (updated: StreamProfile) => Promise<void>;
  onDelete: (id: string) => Promise<void>;
}

function ProfileEditor({ profile, allProfiles, onSave, onDelete }: ProfileEditorProps) {
  const [name, setName] = useState(profile.name);
  const [parentId, setParentId] = useState(profile.parent_id);
  const [config, setConfig] = useState<Record<string, unknown>>(profile.config ?? {});
  const [saving, setSaving] = useState(false);

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

  const handleSave = async () => {
    setSaving(true);
    try {
      await onSave({ ...profile, name, parent_id: parentId, config });
      toast.success(`Profile "${name}" saved`);
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save");
    } finally {
      setSaving(false);
    }
  };

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

        {/* Config fields */}
        <div className="space-y-3">
          <Label className="text-sm font-semibold">Configuration Defaults</Label>
          <p className="text-xs text-muted-foreground">
            Only set fields you want to override. Unset fields will use the
            parent profile&apos;s values (or system defaults).
          </p>

          {CONFIG_FIELDS.map((field) => {
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
                          {field.options.map((o) => (
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
                        min={field.min}
                        max={field.max}
                        step={field.step}
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
                        const defaults: Record<string, unknown> = {
                          resolution: "mobile",
                          game_mode: "fixed",
                          fixed_game: "chaos_arena",
                          fps: 30,
                          bitrate_kbps: 4500,
                          preset: "ultrafast",
                          codec: "libx264",
                          scoreboard_top_n: 5,
                          scoreboard_font_size: 20,
                          scoreboard_recent_hours: 24,
                          vote_overlay_font_scale: 1.0,
                        };
                        setField(field.key, defaults[field.key] ?? "");
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
      </CardContent>
    </Card>
  );
}

export default function ProfilesPage() {
  const [profiles, setProfiles] = useState<StreamProfile[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchProfiles = useCallback(async () => {
    try {
      const data = await api.getProfiles();
      setProfiles(data);
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to load profiles");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchProfiles();
  }, [fetchProfiles]);

  const handleCreate = async () => {
    try {
      await api.createProfile({
        name: "New Profile",
        parent_id: "",
        config: {},
      });
      await fetchProfiles();
      toast.success("Profile created");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to create profile");
    }
  };

  const handleSave = async (updated: StreamProfile) => {
    await api.updateProfile(updated.id, updated);
    await fetchProfiles();
  };

  const handleDelete = async (id: string) => {
    if (!confirm("Delete this profile? Streams using it will lose their parent."))
      return;
    await api.deleteProfile(id);
    await fetchProfiles();
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
              onSave={handleSave}
              onDelete={handleDelete}
            />
          ))}
        </div>
      )}
    </div>
  );
}
