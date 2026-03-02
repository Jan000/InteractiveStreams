"use client";

import { useEffect, useState, useCallback, useRef } from "react";
import {
  Music,
  Play,
  Pause,
  SkipForward,
  Volume2,
  VolumeX,
  RefreshCw,
} from "lucide-react";
import { api, AudioState } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { NumericInput } from "@/components/ui/numeric-input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { toast } from "sonner";

export default function AudioPage() {
  const [audio, setAudio] = useState<AudioState | null>(null);
  const [loading, setLoading] = useState(true);

  // Local state for editable fields
  const [musicVolume, setMusicVolume] = useState(50);
  const [sfxVolume, setSfxVolume] = useState(80);
  const [muted, setMuted] = useState(false);
  const [fadeIn, setFadeIn] = useState(2.0);
  const [fadeOut, setFadeOut] = useState(2.0);
  const [crossfadeOverlap, setCrossfadeOverlap] = useState(1.5);

  // Debounce timer ref for auto-save
  const saveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const initialLoad = useRef(true);

  const fetchAudio = useCallback(async () => {
    try {
      const data = await api.getAudio();
      setAudio(data);
      if (initialLoad.current) {
        setMusicVolume(data.musicVolume);
        setSfxVolume(data.sfxVolume);
        setMuted(data.muted);
        setFadeIn(data.fadeInSeconds);
        setFadeOut(data.fadeOutSeconds);
        setCrossfadeOverlap(data.crossfadeOverlap);
        initialLoad.current = false;
      }
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Failed to load audio");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchAudio();
    const interval = setInterval(fetchAudio, 3000);
    return () => clearInterval(interval);
  }, [fetchAudio]);

  // Auto-save settings with debounce
  const saveSettings = useCallback(
    (updates: Partial<AudioState>) => {
      if (saveTimer.current) clearTimeout(saveTimer.current);
      saveTimer.current = setTimeout(async () => {
        try {
          await api.updateAudio(updates);
        } catch (e) {
          toast.error(
            e instanceof Error ? e.message : "Failed to save audio settings"
          );
        }
      }, 400);
    },
    []
  );

  const handleMusicVolume = (v: number) => {
    setMusicVolume(v);
    saveSettings({ musicVolume: v });
  };

  const handleSfxVolume = (v: number) => {
    setSfxVolume(v);
    saveSettings({ sfxVolume: v });
  };

  const handleMuted = (v: boolean) => {
    setMuted(v);
    saveSettings({ muted: v });
  };

  const handleFadeIn = (v: number) => {
    setFadeIn(v);
    saveSettings({ fadeInSeconds: v });
  };

  const handleFadeOut = (v: number) => {
    setFadeOut(v);
    saveSettings({ fadeOutSeconds: v });
  };

  const handleCrossfadeOverlap = (v: number) => {
    setCrossfadeOverlap(v);
    saveSettings({ crossfadeOverlap: v });
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
      <h1 className="text-2xl font-bold tracking-tight">Audio</h1>

      {/* ── Now Playing ─────────────────────────────────────────────── */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Music className="size-5" />
            Now Playing
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium">
                {audio?.currentTrack || "No track"}
              </p>
              <p className="text-xs text-muted-foreground">
                {audio?.trackCount ?? 0} track(s) in library
              </p>
            </div>
            <div className="flex items-center gap-2">
              <Button
                variant="outline"
                size="icon"
                onClick={async () => {
                  try {
                    if (audio?.playing) {
                      await api.pauseMusic();
                    } else {
                      await api.resumeMusic();
                    }
                    fetchAudio();
                  } catch (e) {
                    toast.error(
                      e instanceof Error ? e.message : "Failed"
                    );
                  }
                }}
              >
                {audio?.playing ? (
                  <Pause className="size-4" />
                ) : (
                  <Play className="size-4" />
                )}
              </Button>
              <Button
                variant="outline"
                size="icon"
                onClick={async () => {
                  try {
                    await api.nextTrack();
                    fetchAudio();
                  } catch (e) {
                    toast.error(
                      e instanceof Error ? e.message : "Failed"
                    );
                  }
                }}
              >
                <SkipForward className="size-4" />
              </Button>
              <Button
                variant="outline"
                size="icon"
                onClick={async () => {
                  try {
                    const res = await api.rescanAudio();
                    toast.success(`Rescan complete: ${res.trackCount} tracks`);
                    fetchAudio();
                  } catch (e) {
                    toast.error(
                      e instanceof Error ? e.message : "Rescan failed"
                    );
                  }
                }}
              >
                <RefreshCw className="size-4" />
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* ── Volume ──────────────────────────────────────────────────── */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Volume2 className="size-5" />
            Volume
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-5">
          {/* Mute toggle */}
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              {muted ? (
                <VolumeX className="size-4 text-muted-foreground" />
              ) : (
                <Volume2 className="size-4" />
              )}
              <Label>Mute All Audio</Label>
            </div>
            <Switch checked={muted} onCheckedChange={handleMuted} />
          </div>

          <Separator />

          {/* Music volume */}
          <div className="space-y-2">
            <Label>Music Volume</Label>
            <div className="flex items-center gap-3">
              <input
                type="range"
                min={0}
                max={100}
                step={1}
                value={musicVolume}
                onChange={(e) => handleMusicVolume(Number(e.target.value))}
                className="flex-1 accent-primary"
              />
              <NumericInput
                value={musicVolume}
                onChange={handleMusicVolume}
                min={0}
                max={100}
                step={1}
                integer
                className="w-20"
              />
            </div>
          </div>

          {/* SFX volume */}
          <div className="space-y-2">
            <Label>SFX Volume</Label>
            <div className="flex items-center gap-3">
              <input
                type="range"
                min={0}
                max={100}
                step={1}
                value={sfxVolume}
                onChange={(e) => handleSfxVolume(Number(e.target.value))}
                className="flex-1 accent-primary"
              />
              <NumericInput
                value={sfxVolume}
                onChange={handleSfxVolume}
                min={0}
                max={100}
                step={1}
                integer
                className="w-20"
              />
            </div>
          </div>
        </CardContent>
      </Card>

      {/* ── Crossfade ───────────────────────────────────────────────── */}
      <Card>
        <CardHeader>
          <CardTitle>Crossfade</CardTitle>
        </CardHeader>
        <CardContent className="space-y-5">
          <p className="text-sm text-muted-foreground">
            Configure smooth transitions between music tracks. The outgoing
            track fades out while the incoming track fades in, overlapping for
            the configured duration.
          </p>

          {/* Fade In */}
          <div className="space-y-2">
            <Label>Fade In Duration (seconds)</Label>
            <p className="text-xs text-muted-foreground">
              How long a new track takes to reach full volume.
            </p>
            <div className="flex items-center gap-3">
              <input
                type="range"
                min={0}
                max={10}
                step={0.1}
                value={fadeIn}
                onChange={(e) => handleFadeIn(Number(e.target.value))}
                className="flex-1 accent-primary"
              />
              <NumericInput
                value={fadeIn}
                onChange={handleFadeIn}
                min={0}
                max={10}
                step={0.1}
                className="w-20"
              />
            </div>
          </div>

          {/* Fade Out */}
          <div className="space-y-2">
            <Label>Fade Out Duration (seconds)</Label>
            <p className="text-xs text-muted-foreground">
              How long the ending track takes to go silent.
            </p>
            <div className="flex items-center gap-3">
              <input
                type="range"
                min={0}
                max={10}
                step={0.1}
                value={fadeOut}
                onChange={(e) => handleFadeOut(Number(e.target.value))}
                className="flex-1 accent-primary"
              />
              <NumericInput
                value={fadeOut}
                onChange={handleFadeOut}
                min={0}
                max={10}
                step={0.1}
                className="w-20"
              />
            </div>
          </div>

          <Separator />

          {/* Crossfade Overlap */}
          <div className="space-y-2">
            <Label>Crossfade Overlap (seconds)</Label>
            <p className="text-xs text-muted-foreground">
              How many seconds before the end of a track the next track starts
              playing. Set to 0 to disable automatic crossfade.
            </p>
            <div className="flex items-center gap-3">
              <input
                type="range"
                min={0}
                max={15}
                step={0.5}
                value={crossfadeOverlap}
                onChange={(e) =>
                  handleCrossfadeOverlap(Number(e.target.value))
                }
                className="flex-1 accent-primary"
              />
              <NumericInput
                value={crossfadeOverlap}
                onChange={handleCrossfadeOverlap}
                min={0}
                max={15}
                step={0.5}
                className="w-20"
              />
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
