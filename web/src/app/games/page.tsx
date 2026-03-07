"use client";

import { useEffect, useState, useCallback, useRef } from "react";
import { Gamepad2, Save, RotateCcw } from "lucide-react";
import { api, GameInfo } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { NumericInput } from "@/components/ui/numeric-input";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { toast } from "sonner";

// Known configurable fields per game with labels and descriptions
const GAME_FIELDS: Record<string, Array<{
  key: string;
  label: string;
  description: string;
  type: "int" | "float";
  min?: number;
  max?: number;
  step?: number;
  defaultValue: number;
}>> = {
  gravity_brawl: [
    { key: "bot_fill", label: "Bot Fill", description: "Fill lobby to N players with bots (0 = disabled)", type: "int", min: 0, max: 50, defaultValue: 0 },
    { key: "game_duration", label: "Game Duration (s)", description: "Length of the battle phase in seconds", type: "float", min: 30, max: 600, step: 10, defaultValue: 120 },
    { key: "lobby_duration", label: "Lobby Duration (s)", description: "How long the lobby waits before starting", type: "float", min: 5, max: 120, step: 5, defaultValue: 30 },
    { key: "min_players", label: "Min Players", description: "Minimum players to start a round (bots count)", type: "int", min: 1, max: 20, defaultValue: 2 },
    { key: "cosmic_event_cooldown", label: "Cosmic Event Cooldown (s)", description: "Cooldown between cosmic events (asteroids, black holes)", type: "float", min: 5, max: 120, step: 5, defaultValue: 30 },
  ],
};

export default function GamesPage() {
  const [games, setGames] = useState<GameInfo[]>([]);
  const [settings, setSettings] = useState<Record<string, Record<string, unknown>>>({});
  const [loading, setLoading] = useState(true);
  const [dirty, setDirty] = useState(false);
  const initialLoad = useRef(true);

  const fetchData = useCallback(async () => {
    try {
      const [gameList, gameSettings] = await Promise.all([
        api.getGames(),
        api.getGameSettings(),
      ]);
      setGames(gameList);
      if (initialLoad.current) {
        setSettings(gameSettings);
        initialLoad.current = false;
      }
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to load game data");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const updateField = (gameId: string, key: string, value: number) => {
    setSettings(prev => ({
      ...prev,
      [gameId]: {
        ...(prev[gameId] || {}),
        [key]: value,
      }
    }));
    setDirty(true);
  };

  const saveAll = async () => {
    try {
      await api.updateGameSettings(settings);
      setDirty(false);
      toast.success("Game settings saved");
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to save settings");
    }
  };

  const resetGame = (gameId: string) => {
    const fields = GAME_FIELDS[gameId];
    if (!fields) return;
    const defaults: Record<string, unknown> = {};
    for (const f of fields) {
      defaults[f.key] = f.defaultValue;
    }
    setSettings(prev => ({ ...prev, [gameId]: defaults }));
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
            Configure game-specific settings that apply across all streams.
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

        return (
          <Card key={game.id}>
            <CardHeader>
              <div className="flex items-center justify-between">
                <div>
                  <CardTitle>{game.name}</CardTitle>
                  <CardDescription className="mt-1">{game.description}</CardDescription>
                </div>
                {fields && (
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={() => resetGame(game.id)}
                  >
                    <RotateCcw className="mr-2 size-3.5" />
                    Reset Defaults
                  </Button>
                )}
              </div>
            </CardHeader>
            <CardContent>
              {fields ? (
                <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
                  {fields.map((field) => {
                    const value = gameSettings[field.key];
                    const numValue = typeof value === "number" ? value : field.defaultValue;
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
              ) : (
                <p className="text-sm text-muted-foreground italic">
                  No configurable settings for this game yet.
                </p>
              )}

              {/* Custom settings (from API but not in known fields) */}
              {(() => {
                const knownKeys = new Set((fields || []).map(f => f.key));
                const extras = Object.entries(gameSettings).filter(([k]) => !knownKeys.has(k));
                if (extras.length === 0) return null;
                return (
                  <>
                    <Separator className="my-4" />
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
            </CardContent>
          </Card>
        );
      })}
    </div>
  );
}
