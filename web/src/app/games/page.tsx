"use client";

import { useEffect, useState, useCallback } from "react";
import { Gamepad2, Save, RotateCcw, Type, AlignLeft, AlignCenter, AlignRight } from "lucide-react";
import { api, GameInfo, TextElementData } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { NumericInput } from "@/components/ui/numeric-input";
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
  type: "int" | "float";
  min?: number;
  max?: number;
  step?: number;
  defaultValue: number;
}>> = {
  gravity_brawl: [
    { key: "bot_fill", label: "Bot Fill", description: "Fill lobby to N players with bots (0 = disabled)", type: "int", min: 0, defaultValue: 0 },
    { key: "game_duration", label: "Game Duration (s)", description: "Length of the battle phase in seconds", type: "float", min: 0, step: 10, defaultValue: 120 },
    { key: "lobby_duration", label: "Lobby Duration (s)", description: "How long the lobby waits before starting", type: "float", min: 0, step: 5, defaultValue: 30 },
    { key: "min_players", label: "Min Players", description: "Minimum players to start a round (bots count)", type: "int", min: 0, defaultValue: 2 },
    { key: "cosmic_event_cooldown", label: "Cosmic Event Cooldown (s)", description: "Cooldown between cosmic events (asteroids, black holes)", type: "float", min: 0, step: 5, defaultValue: 30 },
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
                      Configure position (%), font size, alignment, and visibility for each text element.
                    </p>
                    <div className="space-y-2">
                      {/* Header row */}
                      <div className="hidden sm:grid sm:grid-cols-[1fr_72px_72px_72px_90px_40px] gap-2 px-2 text-xs text-muted-foreground font-medium">
                        <span>Element</span>
                        <span>X %</span>
                        <span>Y %</span>
                        <span>Size</span>
                        <span>Align</span>
                        <span className="text-center">Vis</span>
                      </div>
                      {elements.map((el) => (
                        <div
                          key={el.id}
                          className={cn(
                            "grid grid-cols-2 sm:grid-cols-[1fr_72px_72px_72px_90px_40px] gap-2 items-center rounded-md border p-2 transition-colors",
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
