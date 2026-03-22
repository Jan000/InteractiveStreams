"use client";

import { useEffect, useState, useCallback } from "react";
import {
  api,
  type ScoreEntry,
  type ScoreboardConfig,
  type ScoreboardPanelConfig,
  type PlayerEntry,
  type CountryEntry,
} from "@/lib/api";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { Separator } from "@/components/ui/separator";
import { NumericInput } from "@/components/ui/numeric-input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Trophy,
  Medal,
  Clock,
  Crown,
  Star,
  Users,
  Save,
  Trash2,
  EyeOff,
  Eye,
  Settings2,
  Palette,
  LayoutDashboard,
  Pencil,
  ChevronDown,
  ChevronRight,
  Flag,
  Plus,
  Copy,
  GripVertical,
  Search,
  RotateCcw,
} from "lucide-react";
import { toast } from "sonner";

// ── Default panel config (must match C++ defaults) ──────────────────────
const defaultPanel = (
  title: string,
  contentType = "players",
  timeRange = "alltime",
  group = 0,
  dur = 10,
): ScoreboardPanelConfig => ({
  enabled: true,
  title,
  duration_secs: dur,
  top_n: 5,
  font_size: 20,
  box_width_pct: 30,
  pos_x_pct: 70,
  pos_y_pct: 1,
  align_x: "left",
  align_y: "top",
  opacity: 0.7,
  bg_color: "#0A0A14",
  border_color: "#5082C8",
  title_color: "#FFD700",
  name_color: "#AAAABC",
  points_color: "#58A6FF",
  gold_color: "#FFD700",
  silver_color: "#C8C8C8",
  bronze_color: "#CD7F32",
  content_type: contentType,
  time_range: timeRange,
  game_filter: "",
  group,
  include_bots: false,
  show_flags: contentType === "countries",
  flag_shape: "circle",
  flag_size: 1.0,
  show_names: true,
  show_codes: false,
  value_label: contentType === "countries" ? "wins" : "pts",
  show_avatars: false,
  avatar_shape: "circle",
  avatar_size: 1.0,
});

const defaultConfig: ScoreboardConfig = {
  panels: [
    defaultPanel("ALL TIME", "players", "alltime", 0),
    defaultPanel("LAST 24H", "players", "recent", 0),
    defaultPanel("CURRENT ROUND", "players", "round", 0, 8),
  ],
  recent_hours: 24,
  fade_secs: 1.0,
  chat_interval: 120,
  hidden_players: [],
};

// Panel icon by content type + time range
const panelIcon = (p: ScoreboardPanelConfig) => {
  if (p.content_type === "countries") return <Flag className="size-4 text-green-400" />;
  if (p.time_range === "alltime") return <Trophy className="size-4 text-yellow-400" />;
  if (p.time_range === "recent") return <Clock className="size-4 text-blue-400" />;
  return <Star className="size-4 text-emerald-400" />;
};

const panelLabel = (p: ScoreboardPanelConfig) => {
  const type = p.content_type === "countries" ? "Countries" : "Players";
  const range =
    p.time_range === "alltime" ? "All-Time" :
    p.time_range === "recent" ? "Recent" : "Round";
  return `${type} — ${range}`;
};

// ── Component ───────────────────────────────────────────────────────────
export default function ScoreboardPage() {
  // Config state
  const [config, setConfig] = useState<ScoreboardConfig>(defaultConfig);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);

  // Collapsible panel sections — keyed by panel index
  const [expandedPanels, setExpandedPanels] = useState<Record<number, boolean>>({
    0: true,
  });

  // Player management state
  const [players, setPlayers] = useState<PlayerEntry[]>([]);
  const [editingPlayer, setEditingPlayer] = useState<string | null>(null);
  const [editPoints, setEditPoints] = useState(0);
  const [playerSearch, setPlayerSearch] = useState("");

  // Country management state
  const [countries, setCountries] = useState<CountryEntry[]>([]);
  const [countrySearch, setCountrySearch] = useState("");

  // Live leaderboard
  const [recent, setRecent] = useState<ScoreEntry[]>([]);
  const [allTime, setAllTime] = useState<ScoreEntry[]>([]);

  // ── Data fetching ───────────────────────────────────────────────────
  const fetchConfig = useCallback(async () => {
    try {
      const c = await api.getScoreboardConfig();
      // Ensure panels array exists (backward compat)
      if (!c.panels || !Array.isArray(c.panels)) {
        c.panels = defaultConfig.panels;
      }
      setConfig(c);
    } catch {
      /* backend not running */
    }
  }, []);

  const fetchPlayers = useCallback(async () => {
    try {
      const res = await api.getScoreboardPlayers();
      setPlayers(res.players ?? []);
    } catch {
      /* ignore */
    }
  }, []);

  const fetchCountries = useCallback(async () => {
    try {
      const res = await api.getScoreboardCountries();
      setCountries(res.countries ?? []);
    } catch {
      /* ignore */
    }
  }, []);

  const topN = config.panels.length > 0 ? config.panels[0].top_n : 5;

  const fetchLeaderboards = useCallback(async () => {
    try {
      const [r, a] = await Promise.all([
        api.getScoreboardRecent(topN, config.recent_hours),
        api.getScoreboardAllTime(topN),
      ]);
      setRecent(r.leaderboard ?? []);
      setAllTime(a.leaderboard ?? []);
    } catch {
      /* ignore */
    }
  }, [topN, config.recent_hours]);

  useEffect(() => {
    fetchConfig();
    fetchPlayers();
    fetchCountries();
  }, [fetchConfig, fetchPlayers, fetchCountries]);

  useEffect(() => {
    fetchLeaderboards();
    const iv = setInterval(fetchLeaderboards, 5000);
    return () => clearInterval(iv);
  }, [fetchLeaderboards]);

  // ── Config mutation helpers ─────────────────────────────────────────
  const updatePanel = (
    index: number,
    field: keyof ScoreboardPanelConfig,
    value: unknown,
  ) => {
    setConfig((prev) => {
      const panels = [...prev.panels];
      panels[index] = { ...panels[index], [field]: value };
      return { ...prev, panels };
    });
    setDirty(true);
  };

  const updateGlobal = (field: string, value: unknown) => {
    setConfig((prev) => ({ ...prev, [field]: value }));
    setDirty(true);
  };

  const addPanel = (contentType = "players", timeRange = "alltime") => {
    const groups = config.panels.map((p) => p.group);
    const maxGroup = groups.length > 0 ? Math.max(...groups) : -1;
    const p = defaultPanel(
      contentType === "countries" ? "COUNTRIES" : "PLAYERS",
      contentType,
      timeRange,
      maxGroup + 1,
    );
    setConfig((prev) => ({ ...prev, panels: [...prev.panels, p] }));
    setExpandedPanels((prev) => ({ ...prev, [config.panels.length]: true }));
    setDirty(true);
  };

  const duplicatePanel = (index: number) => {
    const p = { ...config.panels[index], title: config.panels[index].title + " (copy)" };
    setConfig((prev) => {
      const panels = [...prev.panels];
      panels.splice(index + 1, 0, p);
      return { ...prev, panels };
    });
    setDirty(true);
  };

  const removePanel = (index: number) => {
    setConfig((prev) => ({
      ...prev,
      panels: prev.panels.filter((_, i) => i !== index),
    }));
    setDirty(true);
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      await api.updateScoreboardConfig(config);
      toast.success("Scoreboard config saved");
      setDirty(false);
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save");
    } finally {
      setSaving(false);
    }
  };

  // ── Player management ───────────────────────────────────────────────
  const toggleHidden = (userId: string) => {
    setConfig((prev) => {
      const hidden = new Set(prev.hidden_players);
      if (hidden.has(userId)) hidden.delete(userId);
      else hidden.add(userId);
      return { ...prev, hidden_players: [...hidden] };
    });
    setDirty(true);
  };

  const handleEditPoints = async (userId: string) => {
    try {
      await api.updatePlayer(userId, { points: editPoints });
      toast.success("Points updated");
      setEditingPlayer(null);
      fetchPlayers();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleDeletePlayer = async (userId: string) => {
    try {
      await api.deletePlayer(userId);
      toast.success("Player deleted");
      setConfig((prev) => ({
        ...prev,
        hidden_players: prev.hidden_players.filter((id) => id !== userId),
      }));
      fetchPlayers();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleResetAllPlayers = async () => {
    if (!confirm("Reset ALL player data? This deletes all players, scores, and game history. This cannot be undone.")) return;
    try {
      await api.resetAllPlayers();
      toast.success("All players reset");
      fetchPlayers();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleDeleteCountry = async (code: string) => {
    try {
      await api.deleteCountryWins(code);
      toast.success(`Wins for ${code} deleted`);
      fetchCountries();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const handleResetAllCountries = async () => {
    if (!confirm("Reset ALL country wins? This cannot be undone.")) return;
    try {
      await api.resetAllCountryWins();
      toast.success("All country wins reset");
      fetchCountries();
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed");
    }
  };

  const togglePanel = (index: number) =>
    setExpandedPanels((prev) => ({ ...prev, [index]: !prev[index] }));

  // ── Color input helper (render function, NOT a component) ────────────
  const renderColorField = (
    index: number,
    field: keyof ScoreboardPanelConfig,
    label: string,
  ) => {
    const value = (config.panels[index]?.[field] as string) ?? "#000000";
    return (
      <div className="space-y-1" key={field as string}>
        <Label className="text-[10px] text-muted-foreground">{label}</Label>
        <div className="flex items-center gap-1.5">
          <input
            type="color"
            value={value}
            onChange={(e) => updatePanel(index, field, e.target.value)}
            className="h-7 w-7 cursor-pointer rounded border border-border bg-transparent p-0.5"
          />
          <Input
            className="h-7 text-[10px] font-mono flex-1"
            value={value}
            onChange={(e) => updatePanel(index, field, e.target.value)}
          />
        </div>
      </div>
    );
  };

  // ── Panel settings (render function, NOT a component) ───────────────
  const renderPanelSection = (index: number) => {
    const p = config.panels[index];
    if (!p) return null;
    const isExpanded = expandedPanels[index] ?? false;
    const isCountry = p.content_type === "countries";

    return (
      <div className="rounded-lg border">
        {/* Panel header — always visible */}
        <button
          type="button"
          className="flex w-full items-center gap-3 px-4 py-3 hover:bg-muted/50 transition-colors"
          onClick={() => togglePanel(index)}
        >
          <GripVertical className="size-3.5 text-muted-foreground/40" />
          {panelIcon(p)}
          <span className="text-sm font-medium flex-1 text-left">
            {p.title || panelLabel(p)}
          </span>
          <div className="flex items-center gap-2">
            <Badge variant="outline" className="text-[10px]">
              G{p.group}
            </Badge>
            <Badge
              variant={p.enabled ? "default" : "secondary"}
              className="text-[10px]"
            >
              {p.enabled ? (p.top_n > 0 ? `Top ${p.top_n}` : "Disabled") : "Off"}
            </Badge>
            <Button
              size="icon"
              variant="ghost"
              className="size-6"
              onClick={(e) => { e.stopPropagation(); duplicatePanel(index); }}
              title="Duplicate panel"
            >
              <Copy className="size-3" />
            </Button>
            <Button
              size="icon"
              variant="ghost"
              className="size-6 text-destructive hover:text-destructive"
              onClick={(e) => { e.stopPropagation(); removePanel(index); }}
              title="Remove panel"
            >
              <Trash2 className="size-3" />
            </Button>
            {isExpanded ? (
              <ChevronDown className="size-4 text-muted-foreground" />
            ) : (
              <ChevronRight className="size-4 text-muted-foreground" />
            )}
          </div>
        </button>

        {/* Collapsible panel body */}
        {isExpanded && (
          <div className="border-t px-4 pb-4 pt-3 space-y-4">
            {/* Enable + Title + Content Type + Time Range */}
            <div className="grid grid-cols-[1fr_1fr_1fr_auto] gap-3 items-end">
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Title</Label>
                <Input
                  className="h-8 text-sm"
                  value={p.title}
                  onChange={(e) => updatePanel(index, "title", e.target.value)}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Content Type</Label>
                <Select
                  value={p.content_type}
                  onValueChange={(v) => {
                    updatePanel(index, "content_type", v);
                    if (v === "countries") {
                      updatePanel(index, "show_flags", true);
                      updatePanel(index, "value_label", "wins");
                    } else {
                      updatePanel(index, "value_label", "pts");
                    }
                  }}
                >
                  <SelectTrigger className="h-8 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="players">Players</SelectItem>
                    <SelectItem value="countries">Countries</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Time Range</Label>
                <Select
                  value={p.time_range}
                  onValueChange={(v) => updatePanel(index, "time_range", v)}
                >
                  <SelectTrigger className="h-8 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="round">Current Round</SelectItem>
                    <SelectItem value="recent">Recent</SelectItem>
                    <SelectItem value="alltime">All-Time</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="flex items-center gap-2 pb-0.5">
                <Label className="text-xs text-muted-foreground">Enabled</Label>
                <Switch
                  size="sm"
                  checked={p.enabled}
                  onCheckedChange={(v) => updatePanel(index, "enabled", v)}
                />
              </div>
            </div>

            {/* Group + Game Filter + Value Label */}
            <div className="grid grid-cols-3 gap-3">
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Group
                </Label>
                <NumericInput
                  className="h-8 text-sm"
                  integer
                  min={0}
                  max={10}
                  value={p.group}
                  onChange={(v) => updatePanel(index, "group", v)}
                />
                <p className="text-[9px] text-muted-foreground">
                  Same group = cycle, different = simultaneous
                </p>
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Game Filter
                </Label>
                <Select
                  value={p.game_filter || "__all__"}
                  onValueChange={(v) => updatePanel(index, "game_filter", v === "__all__" ? "" : v)}
                >
                  <SelectTrigger className="h-8 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="__all__">All Games</SelectItem>
                    <SelectItem value="chaos_arena">Chaos Arena</SelectItem>
                    <SelectItem value="color_conquest">Color Conquest</SelectItem>
                    <SelectItem value="gravity_brawl">Gravity Brawl</SelectItem>
                    <SelectItem value="country_elimination">Country Elimination</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Value Label
                </Label>
                <Input
                  className="h-8 text-sm"
                  value={p.value_label}
                  onChange={(e) => updatePanel(index, "value_label", e.target.value)}
                  placeholder="pts"
                />
              </div>
            </div>

            {/* Country-specific + display toggles */}
            {isCountry && (
              <div className="flex items-center gap-6 flex-wrap">
                <div className="flex items-center gap-2">
                  <Label className="text-xs text-muted-foreground">Show Flags</Label>
                  <Switch
                    size="sm"
                    checked={p.show_flags}
                    onCheckedChange={(v) => updatePanel(index, "show_flags", v)}
                  />
                </div>
                <div className="flex items-center gap-2">
                  <Label className="text-xs text-muted-foreground">Show Names</Label>
                  <Switch
                    size="sm"
                    checked={p.show_names}
                    onCheckedChange={(v) => updatePanel(index, "show_names", v)}
                  />
                </div>
                <div className="flex items-center gap-2">
                  <Label className="text-xs text-muted-foreground">Show Codes</Label>
                  <Switch
                    size="sm"
                    checked={p.show_codes}
                    onCheckedChange={(v) => updatePanel(index, "show_codes", v)}
                  />
                </div>
                <div className="flex items-center gap-2">
                  <Label className="text-xs text-muted-foreground">Include Bots</Label>
                  <Switch
                    size="sm"
                    checked={p.include_bots}
                    onCheckedChange={(v) => updatePanel(index, "include_bots", v)}
                  />
                </div>
                <div className="space-y-1">
                  <Label className="text-xs text-muted-foreground">Flag Shape</Label>
                  <Select
                    value={p.flag_shape}
                    onValueChange={(v) => updatePanel(index, "flag_shape", v)}
                  >
                    <SelectTrigger className="h-8 text-sm w-24">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="circle">Circle</SelectItem>
                      <SelectItem value="rect">Rectangle</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="space-y-1 w-20">
                  <Label className="text-xs text-muted-foreground">Flag Size</Label>
                  <NumericInput
                    className="h-8 text-sm"
                    min={0.3}
                    max={3.0}
                    step={0.1}
                    value={p.flag_size}
                    onChange={(v) => updatePanel(index, "flag_size", v)}
                  />
                </div>
              </div>
            )}

            {/* Player-specific: Avatar settings */}
            {!isCountry && (
              <div className="flex items-center gap-6 flex-wrap">
                <div className="flex items-center gap-2">
                  <Label className="text-xs text-muted-foreground">Show Avatars</Label>
                  <Switch
                    size="sm"
                    checked={p.show_avatars}
                    onCheckedChange={(v) => updatePanel(index, "show_avatars", v)}
                  />
                </div>
                <div className="space-y-1">
                  <Label className="text-xs text-muted-foreground">Avatar Shape</Label>
                  <Select
                    value={p.avatar_shape}
                    onValueChange={(v) => updatePanel(index, "avatar_shape", v)}
                  >
                    <SelectTrigger className="h-8 text-sm w-24">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="circle">Circle</SelectItem>
                      <SelectItem value="rect">Rectangle</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="space-y-1 w-20">
                  <Label className="text-xs text-muted-foreground">Avatar Size</Label>
                  <NumericInput
                    className="h-8 text-sm"
                    min={0.3}
                    max={3.0}
                    step={0.1}
                    value={p.avatar_size}
                    onChange={(v) => updatePanel(index, "avatar_size", v)}
                  />
                </div>
              </div>
            )}

            {/* Display settings */}
            <div className="grid grid-cols-4 gap-3">
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Duration (s)
                </Label>
                <NumericInput
                  className="h-8 text-sm"
                  min={0}
                  max={120}
                  value={p.duration_secs}
                  onChange={(v) => updatePanel(index, "duration_secs", v)}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Entries
                </Label>
                <NumericInput
                  className="h-8 text-sm"
                  integer
                  min={0}
                  max={20}
                  value={p.top_n}
                  onChange={(v) => updatePanel(index, "top_n", v)}
                />
                <p className="text-[9px] text-muted-foreground">0 = hide panel</p>
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Font Size</Label>
                <NumericInput
                  className="h-8 text-sm"
                  integer
                  min={8}
                  max={48}
                  value={p.font_size}
                  onChange={(v) => updatePanel(index, "font_size", v)}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Opacity</Label>
                <NumericInput
                  className="h-8 text-sm"
                  min={0}
                  max={1}
                  step={0.05}
                  value={p.opacity}
                  onChange={(v) => updatePanel(index, "opacity", v)}
                />
              </div>
            </div>

            {/* Position & Size */}
            <div className="grid grid-cols-3 gap-3">
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Position X (%)
                </Label>
                <NumericInput
                  className="h-8 text-sm"
                  min={-100}
                  max={100}
                  value={p.pos_x_pct}
                  onChange={(v) => updatePanel(index, "pos_x_pct", v)}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Position Y (%)
                </Label>
                <NumericInput
                  className="h-8 text-sm"
                  min={-100}
                  max={100}
                  value={p.pos_y_pct}
                  onChange={(v) => updatePanel(index, "pos_y_pct", v)}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Box Width (%)
                </Label>
                <NumericInput
                  className="h-8 text-sm"
                  min={10}
                  max={80}
                  value={p.box_width_pct}
                  onChange={(v) => updatePanel(index, "box_width_pct", v)}
                />
              </div>
            </div>

            {/* Alignment */}
            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Align X
                </Label>
                <Select
                  value={p.align_x || "left"}
                  onValueChange={(v) => updatePanel(index, "align_x", v)}
                >
                  <SelectTrigger className="h-8 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="left">Left</SelectItem>
                    <SelectItem value="center">Center</SelectItem>
                    <SelectItem value="right">Right</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">
                  Align Y
                </Label>
                <Select
                  value={p.align_y || "top"}
                  onValueChange={(v) => updatePanel(index, "align_y", v)}
                >
                  <SelectTrigger className="h-8 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="top">Top</SelectItem>
                    <SelectItem value="center">Center</SelectItem>
                    <SelectItem value="bottom">Bottom</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>

            {/* Colors — Panel colors */}
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Palette className="size-3.5 text-muted-foreground" />
                <Label className="text-xs font-medium">Panel Colors</Label>
              </div>
              <div className="grid grid-cols-5 gap-3">
                {renderColorField(index, "bg_color", "Background")}
                {renderColorField(index, "border_color", "Border")}
                {renderColorField(index, "title_color", "Title")}
                {renderColorField(index, "name_color", "Name")}
                {renderColorField(index, "points_color", "Points")}
              </div>
            </div>

            {/* Colors — Rank colors */}
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Crown className="size-3.5 text-muted-foreground" />
                <Label className="text-xs font-medium">Rank Colors (Top 3)</Label>
              </div>
              <div className="grid grid-cols-3 gap-3">
                {renderColorField(index, "gold_color", "1st — Gold")}
                {renderColorField(index, "silver_color", "2nd — Silver")}
                {renderColorField(index, "bronze_color", "3rd — Bronze")}
              </div>
            </div>
          </div>
        )}
      </div>
    );
  };

  // ── Rank icon helper ────────────────────────────────────────────────
  const rankIcon = (i: number) => {
    if (i === 0) return <Crown className="size-4 text-yellow-400" />;
    if (i === 1) return <Medal className="size-4 text-gray-300" />;
    if (i === 2) return <Medal className="size-4 text-amber-600" />;
    return (
      <span className="text-xs text-muted-foreground w-4 text-center">
        {i + 1}
      </span>
    );
  };

  // Count active panels & groups
  const activePanels = config.panels.filter(
    (p) => p.enabled && p.top_n > 0,
  ).length;
  const groupSet = new Set(config.panels.map((p) => p.group));

  // ── Render ──────────────────────────────────────────────────────────
  return (
    <div className="flex flex-col gap-6 p-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Trophy className="size-6 text-primary" />
          <div>
            <h1 className="text-2xl font-bold">Scoreboard</h1>
            <p className="text-sm text-muted-foreground">
              Unified overlay panels, player management &amp; live preview
            </p>
          </div>
          <Badge variant="secondary" className="ml-2">
            {activePanels}/{config.panels.length} panels active
          </Badge>
          <Badge variant="outline" className="text-[10px]">
            {groupSet.size} group{groupSet.size !== 1 ? "s" : ""}
          </Badge>
        </div>
        <Button onClick={handleSave} disabled={!dirty || saving} size="sm">
          <Save className="size-4 mr-1" />
          {saving ? "Saving…" : "Save"}
        </Button>
      </div>

      {/* Global Settings + Animation */}
      <Card>
        <CardHeader className="pb-3">
          <div className="flex items-center gap-2">
            <Settings2 className="size-4 text-muted-foreground" />
            <CardTitle className="text-sm font-medium">
              Global Settings &amp; Animation
            </CardTitle>
          </div>
          <CardDescription className="text-xs">
            Controls that affect all panels
          </CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-3 gap-4">
            <div className="space-y-1">
              <Label className="text-xs text-muted-foreground">
                Crossfade Duration (s)
              </Label>
              <NumericInput
                className="h-8 text-sm"
                min={0}
                max={10}
                step={0.1}
                value={config.fade_secs}
                onChange={(v) => updateGlobal("fade_secs", v)}
              />
              <p className="text-[9px] text-muted-foreground">
                Fade between panels. 0 = instant switch.
              </p>
            </div>
            <div className="space-y-1">
              <Label className="text-xs text-muted-foreground">
                Chat Post Interval (s)
              </Label>
              <NumericInput
                className="h-8 text-sm"
                integer
                min={0}
                max={3600}
                value={config.chat_interval}
                onChange={(v) => updateGlobal("chat_interval", v)}
              />
              <p className="text-[9px] text-muted-foreground">
                Post scoreboard to chat. 0 = disabled.
              </p>
            </div>
            <div className="space-y-1">
              <Label className="text-xs text-muted-foreground">
                Recent Hours
              </Label>
              <NumericInput
                className="h-8 text-sm"
                integer
                min={1}
                max={720}
                value={config.recent_hours}
                onChange={(v) => updateGlobal("recent_hours", v)}
              />
              <p className="text-[9px] text-muted-foreground">
                Time window for &ldquo;Recent&rdquo; panels.
              </p>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Panel Configurations — dynamic list */}
      <Card>
        <CardHeader className="pb-3">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <LayoutDashboard className="size-4 text-muted-foreground" />
              <div>
                <CardTitle className="text-sm font-medium">
                  Panel Configuration
                </CardTitle>
                <CardDescription className="text-xs">
                  Panels in the same group cycle; different groups display simultaneously.
                </CardDescription>
              </div>
            </div>
            <div className="flex items-center gap-2">
              <Button
                size="sm"
                variant="outline"
                onClick={() => addPanel("players", "alltime")}
              >
                <Plus className="size-3.5 mr-1" />
                Player Panel
              </Button>
              <Button
                size="sm"
                variant="outline"
                onClick={() => addPanel("countries", "alltime")}
              >
                <Flag className="size-3.5 mr-1" />
                Country Panel
              </Button>
            </div>
          </div>
        </CardHeader>
        <CardContent className="space-y-3">
          {config.panels.length === 0 ? (
            <div className="flex flex-col items-center gap-2 py-8 text-muted-foreground">
              <LayoutDashboard className="size-8" />
              <p className="text-sm">No panels configured</p>
              <p className="text-xs">Add a Player or Country panel to get started</p>
            </div>
          ) : (
            config.panels.map((_, i) => (
              <div key={i}>{renderPanelSection(i)}</div>
            ))
          )}
        </CardContent>
      </Card>

      {/* Player Management */}
      <Card>
        <CardHeader className="pb-3">
          <div className="flex items-center gap-2">
            <Users className="size-4 text-muted-foreground" />
            <CardTitle className="text-sm font-medium">
              Player Management
            </CardTitle>
            <Badge variant="secondary" className="text-xs ml-auto">
              {players.length} players
            </Badge>
            <Button
              size="sm"
              variant="destructive"
              className="ml-2"
              onClick={handleResetAllPlayers}
              disabled={players.length === 0}
            >
              <RotateCcw className="size-3.5 mr-1" />
              Reset All
            </Button>
          </div>
          <CardDescription className="text-xs">
            Hide, edit scores, or delete players. Hidden players are excluded
            from all scoreboards.
          </CardDescription>
        </CardHeader>
        <CardContent>
          {players.length === 0 ? (
            <div className="flex flex-col items-center gap-2 py-8 text-muted-foreground">
              <Users className="size-8" />
              <p className="text-sm">No players yet</p>
            </div>
          ) : (
            <div className="space-y-2">
              {/* Search */}
              <div className="relative">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 size-3.5 text-muted-foreground" />
                <Input
                  className="h-8 pl-8 text-sm"
                  placeholder="Search players…"
                  value={playerSearch}
                  onChange={(e) => setPlayerSearch(e.target.value)}
                />
              </div>
              <div className="space-y-1 max-h-[400px] overflow-y-auto">
              {/* Header */}
              <div className="grid grid-cols-[1fr_100px_80px_80px_100px] gap-2 px-3 py-1.5 text-[10px] font-medium text-muted-foreground uppercase tracking-wider">
                <span>Player</span>
                <span className="text-right">Points</span>
                <span className="text-right">Wins</span>
                <span className="text-right">Games</span>
                <span className="text-right">Actions</span>
              </div>
              <Separator />
              {players
                .filter((p) => {
                  if (!playerSearch) return true;
                  const q = playerSearch.toLowerCase();
                  return p.displayName.toLowerCase().includes(q) || p.userId.toLowerCase().includes(q);
                })
                .map((player) => {
                const isHidden = config.hidden_players.includes(player.userId);
                const isEditing = editingPlayer === player.userId;
                return (
                  <div
                    key={player.userId}
                    className={`grid grid-cols-[1fr_100px_80px_80px_100px] gap-2 items-center px-3 py-1.5 rounded-md ${
                      isHidden
                        ? "bg-muted/30 opacity-60"
                        : "bg-muted/50 hover:bg-muted/70"
                    }`}
                  >
                    {/* Name */}
                    <div className="min-w-0">
                      <span className="text-sm font-medium truncate block">
                        {player.displayName}
                      </span>
                      <span className="text-[10px] text-muted-foreground truncate block">
                        {player.userId}
                      </span>
                    </div>

                    {/* Points */}
                    <div className="text-right">
                      {isEditing ? (
                        <div className="flex items-center gap-1 justify-end">
                          <Input
                            type="number"
                            className="h-6 w-16 text-xs text-right"
                            value={editPoints}
                            onChange={(e) =>
                              setEditPoints(Number(e.target.value))
                            }
                            onKeyDown={(e) => {
                              if (e.key === "Enter")
                                handleEditPoints(player.userId);
                              if (e.key === "Escape") setEditingPlayer(null);
                            }}
                            autoFocus
                          />
                          <Button
                            size="icon"
                            variant="ghost"
                            className="size-6"
                            onClick={() => handleEditPoints(player.userId)}
                          >
                            <Save className="size-3" />
                          </Button>
                        </div>
                      ) : (
                        <span
                          className="text-sm font-mono cursor-pointer hover:text-primary"
                          onClick={() => {
                            setEditingPlayer(player.userId);
                            setEditPoints(player.totalPoints);
                          }}
                          title="Click to edit"
                        >
                          {player.totalPoints}
                          <Pencil className="size-2.5 inline ml-1 opacity-40" />
                        </span>
                      )}
                    </div>

                    {/* Wins */}
                    <span className="text-sm text-right font-mono">
                      {player.totalWins}
                    </span>

                    {/* Games */}
                    <span className="text-sm text-right font-mono">
                      {player.gamesPlayed}
                    </span>

                    {/* Actions */}
                    <div className="flex items-center gap-1 justify-end">
                      <Button
                        size="icon"
                        variant="ghost"
                        className="size-7"
                        onClick={() => toggleHidden(player.userId)}
                        title={isHidden ? "Show player" : "Hide player"}
                      >
                        {isHidden ? (
                          <Eye className="size-3.5" />
                        ) : (
                          <EyeOff className="size-3.5" />
                        )}
                      </Button>
                      <Button
                        size="icon"
                        variant="ghost"
                        className="size-7 text-destructive hover:text-destructive"
                        onClick={() => handleDeletePlayer(player.userId)}
                        title="Delete player"
                      >
                        <Trash2 className="size-3.5" />
                      </Button>
                    </div>
                  </div>
                );
              })}
            </div>
            </div>
          )}
        </CardContent>
      </Card>

      {/* Country Management */}
      <Card>
        <CardHeader className="pb-3">
          <div className="flex items-center gap-2">
            <Flag className="size-4 text-muted-foreground" />
            <CardTitle className="text-sm font-medium">
              Country Management
            </CardTitle>
            <Badge variant="secondary" className="text-xs ml-auto">
              {countries.length} countries
            </Badge>
            <Button
              size="sm"
              variant="destructive"
              className="ml-2"
              onClick={handleResetAllCountries}
              disabled={countries.length === 0}
            >
              <RotateCcw className="size-3.5 mr-1" />
              Reset All
            </Button>
          </div>
          <CardDescription className="text-xs">
            View and manage country wins from Country Elimination.
          </CardDescription>
        </CardHeader>
        <CardContent>
          {countries.length === 0 ? (
            <div className="flex flex-col items-center gap-2 py-8 text-muted-foreground">
              <Flag className="size-8" />
              <p className="text-sm">No country wins yet</p>
            </div>
          ) : (
            <div className="space-y-2">
              {/* Search */}
              <div className="relative">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 size-3.5 text-muted-foreground" />
                <Input
                  className="h-8 pl-8 text-sm"
                  placeholder="Search countries…"
                  value={countrySearch}
                  onChange={(e) => setCountrySearch(e.target.value)}
                />
              </div>
              <div className="space-y-1 max-h-[400px] overflow-y-auto">
                {/* Header */}
                <div className="grid grid-cols-[1fr_100px_80px] gap-2 px-3 py-1.5 text-[10px] font-medium text-muted-foreground uppercase tracking-wider">
                  <span>Country</span>
                  <span className="text-right">Wins</span>
                  <span className="text-right">Actions</span>
                </div>
                <Separator />
                {countries
                  .filter((c) => {
                    if (!countrySearch) return true;
                    return c.countryCode.toLowerCase().includes(countrySearch.toLowerCase());
                  })
                  .map((country) => (
                    <div
                      key={country.countryCode}
                      className="grid grid-cols-[1fr_100px_80px] gap-2 items-center px-3 py-1.5 rounded-md bg-muted/50 hover:bg-muted/70"
                    >
                      <span className="text-sm font-medium">
                        {country.countryCode}
                      </span>
                      <span className="text-sm text-right font-mono">
                        {country.wins}
                      </span>
                      <div className="flex items-center gap-1 justify-end">
                        <Button
                          size="icon"
                          variant="ghost"
                          className="size-7 text-destructive hover:text-destructive"
                          onClick={() => handleDeleteCountry(country.countryCode)}
                          title="Delete country wins"
                        >
                          <Trash2 className="size-3.5" />
                        </Button>
                      </div>
                    </div>
                  ))}
              </div>
            </div>
          )}
        </CardContent>
      </Card>

      {/* Live Leaderboards Preview */}
      <div className="grid gap-6 lg:grid-cols-2">
        {/* Recent */}
        <Card>
          <CardHeader>
            <div className="flex items-center gap-2">
              <Clock className="size-4 text-blue-400" />
              <CardTitle className="text-sm">
                Recent (Last {config.recent_hours}h)
              </CardTitle>
            </div>
            <CardDescription className="text-xs">
              Live preview — auto-refreshes every 5s
            </CardDescription>
          </CardHeader>
          <CardContent>
            {recent.length === 0 ? (
              <p className="text-sm text-muted-foreground text-center py-4">
                No players yet
              </p>
            ) : (
              <div className="space-y-1.5">
                {recent.map((entry, i) => (
                  <div
                    key={`${entry.displayName}-${i}`}
                    className="flex items-center gap-3 rounded-md bg-muted/50 px-3 py-1.5"
                  >
                    <div className="flex items-center justify-center w-6">
                      {rankIcon(i)}
                    </div>
                    <span className="text-sm flex-1 truncate">
                      {entry.displayName}
                    </span>
                    {entry.wins > 0 && (
                      <Badge variant="secondary" className="text-xs gap-1">
                        <Star className="size-3" />
                        {entry.wins}W
                      </Badge>
                    )}
                    <Badge className="text-xs font-bold">
                      {entry.points} pts
                    </Badge>
                  </div>
                ))}
              </div>
            )}
          </CardContent>
        </Card>

        {/* All-Time */}
        <Card>
          <CardHeader>
            <div className="flex items-center gap-2">
              <Trophy className="size-4 text-yellow-400" />
              <CardTitle className="text-sm">All-Time</CardTitle>
            </div>
            <CardDescription className="text-xs">
              Live preview — auto-refreshes every 5s
            </CardDescription>
          </CardHeader>
          <CardContent>
            {allTime.length === 0 ? (
              <p className="text-sm text-muted-foreground text-center py-4">
                No players yet
              </p>
            ) : (
              <div className="space-y-1.5">
                {allTime.map((entry, i) => (
                  <div
                    key={`${entry.displayName}-${i}`}
                    className="flex items-center gap-3 rounded-md bg-muted/50 px-3 py-1.5"
                  >
                    <div className="flex items-center justify-center w-6">
                      {rankIcon(i)}
                    </div>
                    <span className="text-sm flex-1 truncate">
                      {entry.displayName}
                    </span>
                    {entry.wins > 0 && (
                      <Badge variant="secondary" className="text-xs gap-1">
                        <Star className="size-3" />
                        {entry.wins}W
                      </Badge>
                    )}
                    <Badge className="text-xs font-bold">
                      {entry.points} pts
                    </Badge>
                  </div>
                ))}
              </div>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
