"use client";

import { useEffect, useState, useCallback } from "react";
import { Gamepad2, Save, RotateCcw, Type, AlignLeft, AlignCenter, AlignRight } from "lucide-react";
import { api, GameInfo, TextElementData } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { NumericInput } from "@/components/ui/numeric-input";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { Switch } from "@/components/ui/switch";
import { Select, SelectTrigger, SelectValue, SelectContent, SelectItem } from "@/components/ui/select";
import { toast } from "sonner";
import { cn } from "@/lib/utils";

// Known configurable fields per game with labels and descriptions
const GAME_FIELDS: Record<string, Array<{
  key: string;
  label: string;
  description: string;
  type: "int" | "float" | "bool";
  min?: number;
  max?: number;
  step?: number;
  defaultValue: number | boolean;
}>> = {
  gravity_brawl: [
    // --- Collision & Combat ---
    { key: "hit_cooldown", label: "Hit Cooldown (s)", description: "Minimum time between two planets counting a collision hit", type: "float", min: 0, step: 0.05, defaultValue: 0.5 },
    { key: "min_knockback", label: "Min Knockback", description: "Minimum impulse applied on slow collisions so planets push apart", type: "float", min: 0, step: 0.5, defaultValue: 3.0 },
    { key: "collision_min_impulse", label: "Collision Min Impulse", description: "Minimum contact impulse required to count a hit", type: "float", min: 0, step: 0.5, defaultValue: 2.0 },
    { key: "smash_cooldown", label: "Smash Cooldown (s)", description: "Seconds before a player can smash again", type: "float", min: 0, step: 0.05, defaultValue: 0.8 },
    { key: "smash_impulse", label: "Smash Impulse", description: "Base force applied when a player smashes (dashes)", type: "float", min: 0, step: 0.5, defaultValue: 12.0 },
    { key: "supernova_radius", label: "Supernova Radius (m)", description: "Explosion radius of the supernova ability", type: "float", min: 0, step: 0.5, defaultValue: 8.0 },
    { key: "supernova_force", label: "Supernova Force", description: "Repulsion force of the supernova explosion", type: "float", min: 0, step: 1, defaultValue: 30.0 },
    { key: "respawn_cooldown", label: "Respawn Cooldown (s)", description: "Seconds a player must wait after dying before re-joining", type: "float", min: 0, step: 1, defaultValue: 45.0 },
    // --- Physics ---
    { key: "max_speed", label: "Max Speed (m/s)", description: "Maximum velocity a planet can reach", type: "float", min: 0, step: 0.5, defaultValue: 25.0 },
    { key: "restitution", label: "Restitution (Bounciness)", description: "Elasticity of planet collisions (0 = no bounce, 1 = full bounce)", type: "float", min: 0, step: 0.05, defaultValue: 0.7 },
    { key: "linear_damping", label: "Linear Damping", description: "Velocity damping applied each frame (higher = more friction)", type: "float", min: 0, step: 0.05, defaultValue: 0.5 },
    // --- Orbit & Spawning ---
    { key: "spawn_radius_factor", label: "Spawn Radius Factor", description: "Spawn distance from center as a factor of arena radius", type: "float", min: 0.01, step: 0.01, defaultValue: 0.92 },
    { key: "spawn_orbit_speed", label: "Spawn Orbit Speed", description: "Initial tangential speed when a player or bot spawns", type: "float", min: 0, step: 0.1, defaultValue: 7.25 },
    { key: "safe_orbit_radius_factor", label: "Safe Orbit Radius Factor", description: "Target orbit distance used by the orbital stabilizer", type: "float", min: 0.01, step: 0.01, defaultValue: 0.78 },
    { key: "orbital_gravity_strength", label: "Orbital Gravity Strength", description: "Base inward pull that keeps planets in orbit", type: "float", min: 0, step: 0.1, defaultValue: 8.0 },
    { key: "orbital_outer_pull_multiplier", label: "Outer Pull Multiplier", description: "Extra inward pull for planets drifting too far outward", type: "float", min: 0, step: 0.05, defaultValue: 1.25 },
    { key: "orbital_safe_zone_pull_multiplier", label: "Safe Zone Pull Multiplier", description: "Reduced inward pull while planets are near the safe orbit", type: "float", min: 0, step: 0.05, defaultValue: 0.35 },
    { key: "orbital_tangential_strength", label: "Orbital Tangential Strength", description: "Sideways force that keeps planets circling instead of dropping inward", type: "float", min: 0, step: 0.1, defaultValue: 8.75 },
    // --- Black Hole ---
    { key: "black_hole_radius", label: "Black Hole Radius (m)", description: "Visual and physics radius of the black hole in meters", type: "float", min: 0.5, step: 0.25, defaultValue: 2.5 },
    { key: "black_hole_gravity_strength", label: "Black Hole Gravity Strength", description: "Base pull strength of the black hole", type: "float", min: 0, step: 0.1, defaultValue: 6.0 },
    { key: "black_hole_consume_size_factor", label: "Consume Size Growth", description: "Additional black hole pull gained when it consumes a player, scaled by player size", type: "float", min: 0, step: 0.05, defaultValue: 1.75 },
    { key: "black_hole_gravity_cap", label: "Black Hole Gravity Cap", description: "Maximum black hole pull applied in a single physics step", type: "float", min: 0, step: 0.5, defaultValue: 30.0 },
    { key: "black_hole_kill_radius_multiplier", label: "Black Hole Kill Radius", description: "Multiplier for the actual kill radius around the black hole", type: "float", min: 0, step: 0.05, defaultValue: 1.0 },
    // --- Cosmic Events ---
    { key: "cosmic_event_cooldown", label: "Cosmic Event Cooldown (s)", description: "Cooldown between cosmic events", type: "float", min: 0, step: 5, defaultValue: 30 },
    { key: "cosmic_event_duration", label: "Cosmic Event Duration (s)", description: "How long a cosmic event lasts", type: "float", min: 0, step: 1, defaultValue: 10 },
    { key: "event_gravity_multiplier", label: "Event Gravity Multiplier", description: "Multiplier applied to black hole pull during cosmic surge events", type: "float", min: 0, step: 0.1, defaultValue: 2.2 },
    { key: "anomaly_spawn_interval", label: "Anomaly Spawn Interval (s)", description: "Seconds between anomaly item spawns during cosmic events", type: "float", min: 0, step: 0.5, defaultValue: 3.0 },
    // --- Timing ---
    { key: "epoch_duration_seconds", label: "Epoch Duration (s)", description: "Duration of each difficulty epoch (black hole grows each epoch)", type: "float", min: 0, step: 5, defaultValue: 60 },
    { key: "afk_timeout_seconds", label: "AFK Timeout (s)", description: "Seconds of inactivity before a player is kicked", type: "float", min: 0, step: 5, defaultValue: 120 },
    // --- Camera ---
    { key: "camera_zoom_enabled", label: "Dynamic Camera Zoom", description: "Smoothly zoom to keep all players visible on screen", type: "bool", defaultValue: true },
    { key: "camera_zoom_speed", label: "Camera Zoom Speed", description: "How quickly the camera adjusts zoom level (higher = faster)", type: "float", min: 0.01, step: 0.1, defaultValue: 3.5 },
    { key: "camera_buffer_meters", label: "Screen Margin (m)", description: "Buffer zone around players for mobile safe area", type: "float", min: 0, step: 0.5, defaultValue: 1.5 },
    { key: "camera_min_zoom", label: "Camera Min Zoom", description: "Minimum zoom level (lower = camera can zoom out further)", type: "float", min: 0.01, step: 0.01, defaultValue: 0.3 },
    { key: "camera_max_zoom", label: "Camera Max Zoom", description: "Maximum zoom-in level when players are clustered together", type: "float", min: 0.01, step: 0.1, defaultValue: 2.5 },
    // --- Planet Sizes ---
    { key: "tier_radius_asteroid", label: "Asteroid Size", description: "Visual radius of Asteroid tier planets (meters)", type: "float", min: 0.01, step: 0.05, defaultValue: 0.5 },
    { key: "tier_radius_ice", label: "Ice Planet Size", description: "Visual radius of Ice Planet tier (meters)", type: "float", min: 0.01, step: 0.05, defaultValue: 0.7 },
    { key: "tier_radius_gas", label: "Gas Giant Size", description: "Visual radius of Gas Giant tier (meters)", type: "float", min: 0.01, step: 0.05, defaultValue: 0.95 },
    { key: "tier_radius_star", label: "Star Size", description: "Visual radius of Star tier (meters)", type: "float", min: 0.01, step: 0.05, defaultValue: 1.25 },
    // --- Visuals ---
    { key: "enable_post_processing", label: "Post-Processing", description: "Enable GPU post-processing effects (bloom, vignette, etc.)", type: "bool", defaultValue: true },
    { key: "enable_planet_glow", label: "Planet Glow", description: "Render a glow halo around planets", type: "bool", defaultValue: true },
    { key: "max_particles", label: "Max Particles", description: "Maximum number of particle effects at once", type: "int", min: 0, step: 50, defaultValue: 500 },
    // --- Audio ---
    { key: "sfx_enabled", label: "SFX Enabled", description: "Play sound effects for game events", type: "bool", defaultValue: true },
    { key: "sfx_volume", label: "SFX Volume", description: "Volume of sound effects (0–100)", type: "float", min: 0, max: 100, step: 1, defaultValue: 80 },
    // --- Bots ---
    { key: "bot_fill", label: "Bot Fill", description: "Fill lobby to N players with bots (0 = disabled)", type: "int", min: 0, defaultValue: 0 },
    { key: "bot_kill_feed", label: "Bots in Kill Feed", description: "Show bot kills and deaths in the kill feed overlay", type: "bool", defaultValue: false },
    { key: "bot_respawn", label: "Bot Respawn", description: "Respawn dead bots during the match after a delay", type: "bool", defaultValue: false },
    { key: "bot_respawn_delay", label: "Bot Respawn Delay (s)", description: "Seconds before a dead bot re-enters the arena", type: "float", min: 0, step: 0.5, defaultValue: 5.0 },
    { key: "bot_action_interval", label: "Bot Action Interval (s)", description: "Seconds between bot AI decisions (lower = more active)", type: "float", min: 0.01, step: 0.05, defaultValue: 0.3 },
    { key: "bot_smash_chance", label: "Bot Smash Chance", description: "Base probability of a bot smashing each tick", type: "float", min: 0, max: 1, step: 0.05, defaultValue: 0.2 },
    { key: "bot_danger_smash_chance", label: "Bot Danger Smash Chance", description: "Smash probability when a bot is near the black hole", type: "float", min: 0, max: 1, step: 0.05, defaultValue: 0.6 },
    { key: "bot_event_smash_chance", label: "Bot Event Smash Chance", description: "Smash probability during cosmic events", type: "float", min: 0, max: 1, step: 0.05, defaultValue: 0.7 },
  ],
  country_elimination: [
    // --- Arena ---
    { key: "arena_speed", label: "Arena Speed (rad/s)", description: "Initial rotation speed of the arena ring", type: "float", min: 0.05, step: 0.05, defaultValue: 0.3 },
    { key: "arena_speed_increase", label: "Arena Acceleration (/s)", description: "How fast the arena rotation accelerates during battle", type: "float", min: 0, step: 0.01, defaultValue: 0.03 },
    { key: "gap_expansion_rate", label: "Gap Expansion Rate (rad/s)", description: "How quickly the arena gap widens during battle", type: "float", min: 0, step: 0.005, defaultValue: 0.02 },
    { key: "gap_max", label: "Gap Maximum (rad)", description: "Maximum gap size in the arena ring", type: "float", min: 0.3, max: 2.5, step: 0.1, defaultValue: 1.2 },
    // --- Ball Physics ---
    { key: "initial_speed", label: "Initial Ball Speed (m/s)", description: "Starting velocity of balls when spawned", type: "float", min: 0.5, step: 0.5, defaultValue: 5.0 },
    { key: "ball_speed_increase", label: "Ball Speed Increase (/s)", description: "How much ball speed increases per second during battle", type: "float", min: 0, step: 0.1, defaultValue: 0.5 },
    { key: "max_ball_speed", label: "Max Ball Speed (m/s)", description: "Maximum ball velocity", type: "float", min: 1, step: 0.5, defaultValue: 15.0 },
    { key: "restitution", label: "Restitution (Bounciness)", description: "Elasticity of ball collisions (0 = no bounce, 1 = full bounce)", type: "float", min: 0, max: 1, step: 0.05, defaultValue: 0.95 },
    // --- Timing ---
    { key: "round_duration", label: "Round Duration (s)", description: "Maximum time per round before forced end", type: "float", min: 10, step: 5, defaultValue: 120 },
    { key: "lobby_duration", label: "Lobby Duration (s)", description: "How long the lobby waits before starting countdown", type: "float", min: 1, step: 1, defaultValue: 5 },
    { key: "round_end_duration", label: "Round End Duration (s)", description: "How long the winner overlay is shown between rounds", type: "float", min: 1, step: 0.5, defaultValue: 4 },
    // --- Gameplay ---
    { key: "min_players", label: "Min Players", description: "Minimum players (including bots) required to start a round", type: "int", min: 2, step: 1, defaultValue: 2 },
    { key: "champion_threshold", label: "Champion Threshold", description: "Number of round wins needed to become champion (game over)", type: "int", min: 2, step: 1, defaultValue: 4 },
    { key: "max_entries_per_player", label: "Max Entries Per Player", description: "How many balls a single player can have simultaneously (join N times)", type: "int", min: 1, max: 10, step: 1, defaultValue: 1 },
    // --- Bots ---
    { key: "bot_fill", label: "Bot Fill", description: "Fill arena to this many players with bots (0 = disabled)", type: "int", min: 0, step: 1, defaultValue: 8 },
    { key: "bot_respawn", label: "Bot Respawn", description: "Respawn eliminated bots during battle phase", type: "bool", defaultValue: true },
    { key: "bot_respawn_delay", label: "Bot Respawn Delay (s)", description: "Seconds before an eliminated bot re-enters the arena", type: "float", min: 0.5, step: 0.5, defaultValue: 3.0 },
    // --- Eliminated Display ---
    { key: "max_eliminated_visible", label: "Max Eliminated Visible", description: "Maximum number of eliminated balls visible on the floor (0 = no limit)", type: "int", min: 0, step: 1, defaultValue: 20 },
    { key: "elim_linger_duration", label: "Eliminated Linger Duration (s)", description: "How long eliminated balls rest on the floor before fading out", type: "float", min: 0.5, step: 0.5, defaultValue: 8.0 },
    { key: "elim_fade_duration", label: "Eliminated Fade Duration (s)", description: "How long the fade-out animation takes for removed eliminated balls", type: "float", min: 0.1, step: 0.1, defaultValue: 2.0 },
    { key: "elim_infinite_linger", label: "Infinite Linger", description: "Eliminated balls stay forever (no time-based fading). Max visible count still applies.", type: "bool", defaultValue: false },
    { key: "elim_persist_rounds", label: "Persist Across Rounds", description: "Eliminated balls remain on the floor between rounds instead of being removed", type: "bool", defaultValue: false },
    // --- Visual ---
    { key: "flag_shape_rect", label: "Rectangular Flags", description: "Display flags as rectangles instead of circles (hitbox adjusts accordingly)", type: "bool", defaultValue: false },
    { key: "flag_outline", label: "Flag Outline", description: "Show a white outline/border around flags", type: "bool", defaultValue: true },
    { key: "flag_outline_thickness", label: "Flag Outline Thickness", description: "Thickness of the flag outline in pixels (only when outline is enabled)", type: "float", min: 0, max: 10, step: 0.5, defaultValue: 1.5 },
    { key: "avatar_outline_thickness", label: "Avatar Outline Thickness", description: "Thickness of the outline ring around profile pictures below balls", type: "float", min: 0, max: 5, step: 0.5, defaultValue: 1.0 },
    { key: "name_text_scale", label: "Name Text Scale", description: "Scale multiplier for player name text below balls (1.0 = default)", type: "float", min: 0.3, max: 3.0, step: 0.1, defaultValue: 1.0 },
    { key: "label_text_scale", label: "Label Text Scale", description: "Scale multiplier for country label text on balls without flags (1.0 = default)", type: "float", min: 0.3, max: 3.0, step: 0.1, defaultValue: 1.0 },
    { key: "avatar_scale", label: "Avatar Scale", description: "Scale multiplier for profile picture circles below balls (1.0 = default)", type: "float", min: 0.3, max: 3.0, step: 0.1, defaultValue: 1.0 },
    { key: "rainbow_ring", label: "Rainbow Ring", description: "Animate the arena ring with rainbow colors", type: "bool", defaultValue: true },
    { key: "allow_reentry", label: "Allow Re-entry", description: "Players that bounce back into the arena are revived and continue playing", type: "bool", defaultValue: true },
    { key: "show_bot_names", label: "Show Bot Names", description: "Display name labels below bot players", type: "bool", defaultValue: true },
    // --- Quiz ---
    { key: "quiz_enabled", label: "Enable Quiz", description: "Show periodic geography quiz questions during the game", type: "bool", defaultValue: true },
    { key: "quiz_interval", label: "Quiz Interval (s)", description: "Seconds between quiz questions (cooldown after each question)", type: "float", min: 5, max: 300, step: 1, defaultValue: 30 },
    { key: "quiz_duration", label: "Quiz Duration (s)", description: "How many seconds players have to answer each question", type: "float", min: 5, max: 60, step: 1, defaultValue: 15 },
    { key: "quiz_points", label: "Quiz Points", description: "Points awarded for a correct quiz answer", type: "int", min: 0, max: 1000, step: 10, defaultValue: 50 },
    { key: "quiz_shield_secs", label: "Quiz Shield Duration (s)", description: "Shield duration awarded to active players for correct answers (0 = no shield)", type: "float", min: 0, max: 60, step: 1, defaultValue: 10 },
  ],
};

export default function GamesPage() {
  const [games, setGames] = useState<GameInfo[]>([]);
  const [settings, setSettings] = useState<Record<string, Record<string, unknown>>>({});
  const [textElements, setTextElements] = useState<Record<string, TextElementData[]>>({});
  const [textDefaults, setTextDefaults] = useState<Record<string, TextElementData[]>>({});
  const [loading, setLoading] = useState(true);
  const [dirty, setDirty] = useState(false);

  const fetchData = useCallback(async () => {
    try {
      const [gameList, gameSettings, textEls] = await Promise.all([
        api.getGames(),
        api.getGameSettings(),
        api.getTextElements(),
      ]);
      setGames(gameList);
      setSettings(gameSettings);
      setTextDefaults(JSON.parse(JSON.stringify(textEls)));
      setTextElements(textEls);
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to load game data");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const updateField = (gameId: string, key: string, value: number | boolean) => {
    setSettings(prev => ({
      ...prev,
      [gameId]: {
        ...(prev[gameId] || {}),
        [key]: value,
      }
    }));
    setDirty(true);
  };

  const updateTextElement = (gameId: string, elementId: string, field: keyof TextElementData, value: unknown) => {
    setTextElements(prev => {
      const elements = [...(prev[gameId] || [])];
      const idx = elements.findIndex(e => e.id === elementId);
      if (idx === -1) return prev;
      elements[idx] = { ...elements[idx], [field]: value };
      return { ...prev, [gameId]: elements };
    });
    setDirty(true);
  };

  const saveAll = async () => {
    try {
      // Merge text elements into settings payload
      const payload: Record<string, Record<string, unknown>> = {};
      for (const [gameId, s] of Object.entries(settings)) {
        payload[gameId] = { ...s };
      }
      for (const [gameId, elements] of Object.entries(textElements)) {
        if (!payload[gameId]) payload[gameId] = {};
        payload[gameId].text_elements = elements;
      }
      await api.updateGameSettings(payload);
      await fetchData();
      setDirty(false);
      toast.success("Game settings saved and applied");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save settings");
    }
  };

  const resetGame = (gameId: string) => {
    const fields = GAME_FIELDS[gameId];
    if (fields) {
      const defaults: Record<string, unknown> = {};
      for (const f of fields) {
        defaults[f.key] = f.defaultValue;
      }
      setSettings(prev => ({ ...prev, [gameId]: defaults }));
    }
    // Reset text elements to defaults
    if (textDefaults[gameId]) {
      setTextElements(prev => ({
        ...prev,
        [gameId]: JSON.parse(JSON.stringify(textDefaults[gameId]))
      }));
    }
    setDirty(true);
  };

  if (loading) {
    return (
      <div className="space-y-4">
        <Skeleton className="h-8 w-48" />
        <Skeleton className="h-64 w-full" />
      </div>
    );
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold flex items-center gap-2">
            <Gamepad2 className="size-6" />
            Game Settings
          </h1>
          <p className="text-sm text-muted-foreground mt-1">
            Configure game-specific settings and text layout that apply across all streams.
          </p>
        </div>
        <Button onClick={saveAll} disabled={!dirty} size="sm">
          <Save className="mr-2 size-4" />
          Save All
        </Button>
      </div>

      {games.map((game) => {
        const fields = GAME_FIELDS[game.id];
        const gameSettings = settings[game.id] || {};
        const elements = textElements[game.id] || [];

        return (
          <Card key={game.id}>
            <CardHeader>
              <div className="flex items-center justify-between">
                <div>
                  <CardTitle>{game.name}</CardTitle>
                  <CardDescription className="mt-1">{game.description}</CardDescription>
                </div>
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() => resetGame(game.id)}
                >
                  <RotateCcw className="mr-2 size-3.5" />
                  Reset Defaults
                </Button>
              </div>
            </CardHeader>
            <CardContent className="space-y-6">
              {/* Gameplay settings */}
              {fields && (
                <div>
                  <h3 className="text-sm font-medium mb-3">Gameplay</h3>
                  <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
                    {fields.map((field) => {
                      if (field.type === "bool") {
                        const checked = typeof gameSettings[field.key] === "boolean" ? gameSettings[field.key] as boolean : field.defaultValue as boolean;
                        return (
                          <div key={field.key} className="flex items-center justify-between rounded-md border p-3">
                            <div className="space-y-0.5">
                              <Label htmlFor={`${game.id}-${field.key}`}>{field.label}</Label>
                              <p className="text-xs text-muted-foreground">{field.description}</p>
                            </div>
                            <Switch
                              id={`${game.id}-${field.key}`}
                              checked={checked}
                              onCheckedChange={(v) => updateField(game.id, field.key, v)}
                            />
                          </div>
                        );
                      }
                      const value = gameSettings[field.key];
                      const numValue = typeof value === "number" ? value : field.defaultValue as number;
                      return (
                        <div key={field.key} className="space-y-1.5">
                          <Label htmlFor={`${game.id}-${field.key}`}>{field.label}</Label>
                          <NumericInput
                            id={`${game.id}-${field.key}`}
                            value={numValue}
                            onChange={(v) => updateField(game.id, field.key, v)}
                            min={field.min}
                            max={field.max}
                            step={field.step ?? (field.type === "int" ? 1 : 0.1)}
                          />
                          <p className="text-xs text-muted-foreground">{field.description}</p>
                        </div>
                      );
                    })}
                  </div>
                </div>
              )}

              {/* Text layout editor */}
              {elements.length > 0 && (
                <>
                  {fields && <Separator />}
                  <div>
                    <h3 className="text-sm font-medium mb-3 flex items-center gap-2">
                      <Type className="size-4" />
                      Text Layout
                    </h3>
                    <p className="text-xs text-muted-foreground mb-4">
                      Configure position (%), font size, alignment, color, and visibility for each text element.
                    </p>
                    <div className="space-y-2">
                      {/* Header row */}
                      <div className="hidden sm:grid sm:grid-cols-[1fr_72px_72px_72px_90px_100px_40px] gap-2 px-2 text-xs text-muted-foreground font-medium">
                        <span>Element</span>
                        <span>X %</span>
                        <span>Y %</span>
                        <span>Size</span>
                        <span>Align</span>
                        <span>Color</span>
                        <span className="text-center">Vis</span>
                      </div>
                      {elements.map((el) => (
                        <div
                          key={el.id}
                          className={cn(
                            "grid grid-cols-2 sm:grid-cols-[1fr_72px_72px_72px_90px_100px_40px] gap-2 items-center rounded-md border p-2 transition-colors",
                            !el.visible && "opacity-50 bg-muted/30"
                          )}
                        >
                          {/* Label */}
                          <div className="col-span-2 sm:col-span-1">
                            <span className="text-sm font-medium">{el.label}</span>
                            <span className="text-xs text-muted-foreground ml-1.5">({el.id})</span>
                          </div>

                          {/* X % */}
                          <NumericInput
                            value={el.x}
                            onChange={(v) => updateTextElement(game.id, el.id, "x", v)}
                            step={0.5}
                            className="h-8 text-xs"
                          />

                          {/* Y % */}
                          <NumericInput
                            value={el.y}
                            onChange={(v) => updateTextElement(game.id, el.id, "y", v)}
                            step={0.5}
                            className="h-8 text-xs"
                          />

                          {/* Font size */}
                          <NumericInput
                            value={el.font_size}
                            onChange={(v) => updateTextElement(game.id, el.id, "font_size", v)}
                            min={1}
                            step={1}
                            integer
                            className="h-8 text-xs"
                          />

                          {/* Align */}
                          <Select
                            value={el.align}
                            onValueChange={(v) => updateTextElement(game.id, el.id, "align", v)}
                          >
                            <SelectTrigger size="sm" className="h-8 text-xs w-full">
                              <SelectValue />
                            </SelectTrigger>
                            <SelectContent>
                              <SelectItem value="left">
                                <AlignLeft className="size-3.5 mr-1 inline" /> Left
                              </SelectItem>
                              <SelectItem value="center">
                                <AlignCenter className="size-3.5 mr-1 inline" /> Center
                              </SelectItem>
                              <SelectItem value="right">
                                <AlignRight className="size-3.5 mr-1 inline" /> Right
                              </SelectItem>
                            </SelectContent>
                          </Select>

                          {/* Color */}
                          {(() => {
                            const defaultEl = (textDefaults[game.id] || []).find(d => d.id === el.id);
                            const defaultColor = defaultEl?.color || "";
                            const displayColor = el.color || defaultColor;
                            return (
                              <div className="flex items-center gap-1">
                                <input
                                  type="color"
                                  value={displayColor ? (displayColor.length >= 7 ? displayColor.slice(0, 7) : "#ffffff") : "#ffffff"}
                                  onChange={(e) => {
                                    const hex = e.target.value.toUpperCase();
                                    const alpha = el.color && el.color.length === 9 ? el.color.slice(7) : "FF";
                                    updateTextElement(game.id, el.id, "color", hex + alpha);
                                  }}
                                  className="h-8 w-8 rounded border cursor-pointer p-0.5"
                                />
                                <Input
                                  value={el.color || ""}
                                  onChange={(e) => updateTextElement(game.id, el.id, "color", e.target.value)}
                                  placeholder={defaultColor || "Game default"}
                                  className="h-8 text-xs w-[80px] font-mono"
                                />
                              </div>
                            );
                          })()}

                          {/* Visible toggle */}
                          <div className="flex justify-center">
                            <Switch
                              size="sm"
                              checked={el.visible}
                              onCheckedChange={(v) => updateTextElement(game.id, el.id, "visible", v)}
                            />
                          </div>
                        </div>
                      ))}
                    </div>
                  </div>
                </>
              )}

              {/* Extra settings from API not in known fields */}
              {(() => {
                const knownKeys = new Set((fields || []).map(f => f.key));
                knownKeys.add("text_elements"); // hide from "additional"
                const extras = Object.entries(gameSettings).filter(([k]) => !knownKeys.has(k));
                if (extras.length === 0) return null;
                return (
                  <>
                    <Separator />
                    <div className="space-y-2">
                      <h4 className="text-sm font-medium text-muted-foreground">Additional Settings</h4>
                      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
                        {extras.map(([key, val]) => (
                          <div key={key} className="space-y-1.5">
                            <Label htmlFor={`${game.id}-${key}`}>{key}</Label>
                            <NumericInput
                              id={`${game.id}-${key}`}
                              value={typeof val === "number" ? val : 0}
                              onChange={(v) => updateField(game.id, key, v)}
                            />
                          </div>
                        ))}
                      </div>
                    </div>
                  </>
                );
              })()}

              {/* Games with no fields and no text elements */}
              {!fields && elements.length === 0 && (
                <p className="text-sm text-muted-foreground italic">
                  No configurable settings for this game yet.
                </p>
              )}
            </CardContent>
          </Card>
        );
      })}
    </div>
  );
}
