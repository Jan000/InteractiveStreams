"use client";

import { useEffect, useState, useCallback } from "react";
import { api, type ScoreEntry } from "@/lib/api";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Trophy, Medal, Clock, Crown, Star, Users } from "lucide-react";

export default function ScoreboardPage() {
  const [recent, setRecent] = useState<ScoreEntry[]>([]);
  const [allTime, setAllTime] = useState<ScoreEntry[]>([]);
  const [recentLimit, setRecentLimit] = useState(10);
  const [recentHours, setRecentHours] = useState(24);
  const [allTimeLimit, setAllTimeLimit] = useState(5);

  const fetchData = useCallback(async () => {
    try {
      const [r, a] = await Promise.all([
        api.getScoreboardRecent(recentLimit, recentHours),
        api.getScoreboardAllTime(allTimeLimit),
      ]);
      setRecent(r.leaderboard ?? []);
      setAllTime(a.leaderboard ?? []);
    } catch {
      /* backend may not be running */
    }
  }, [recentLimit, recentHours, allTimeLimit]);

  useEffect(() => {
    fetchData();
    const iv = setInterval(fetchData, 5000);
    return () => clearInterval(iv);
  }, [fetchData]);

  const rankIcon = (i: number) => {
    if (i === 0) return <Crown className="size-4 text-yellow-400" />;
    if (i === 1) return <Medal className="size-4 text-gray-300" />;
    if (i === 2) return <Medal className="size-4 text-amber-600" />;
    return <span className="text-xs text-muted-foreground w-4 text-center">{i + 1}</span>;
  };

  return (
    <div className="flex flex-col gap-6 p-6">
      <div className="flex items-center gap-3">
        <Trophy className="size-6 text-primary" />
        <div>
          <h1 className="text-2xl font-bold">Scoreboard</h1>
          <p className="text-sm text-muted-foreground">
            Player rankings across all games
          </p>
        </div>
      </div>

      {/* Settings row */}
      <Card>
        <CardHeader className="pb-3">
          <CardTitle className="text-sm font-medium">Display Settings</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="flex flex-wrap items-end gap-4">
            <div className="space-y-1">
              <Label className="text-xs">Recent: Top N</Label>
              <Input
                type="number"
                min={1}
                max={50}
                value={recentLimit}
                onChange={(e) => setRecentLimit(Number(e.target.value) || 10)}
                className="w-20 h-8 text-sm"
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">Recent: Hours</Label>
              <Input
                type="number"
                min={1}
                max={168}
                value={recentHours}
                onChange={(e) => setRecentHours(Number(e.target.value) || 24)}
                className="w-20 h-8 text-sm"
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">All-Time: Top N</Label>
              <Input
                type="number"
                min={1}
                max={50}
                value={allTimeLimit}
                onChange={(e) => setAllTimeLimit(Number(e.target.value) || 5)}
                className="w-20 h-8 text-sm"
              />
            </div>
          </div>
        </CardContent>
      </Card>

      <div className="grid gap-6 lg:grid-cols-2">
        {/* Recent leaderboard */}
        <Card>
          <CardHeader>
            <div className="flex items-center gap-2">
              <Clock className="size-4 text-blue-400" />
              <CardTitle>Recent (Last {recentHours}h)</CardTitle>
            </div>
            <CardDescription>Top {recentLimit} players by points</CardDescription>
          </CardHeader>
          <CardContent>
            {recent.length === 0 ? (
              <div className="flex flex-col items-center gap-2 py-8 text-muted-foreground">
                <Users className="size-8" />
                <p className="text-sm">No players yet</p>
              </div>
            ) : (
              <div className="space-y-2">
                {recent.map((entry, i) => (
                  <div
                    key={`${entry.displayName}-${i}`}
                    className="flex items-center gap-3 rounded-md bg-muted/50 px-3 py-2"
                  >
                    <div className="flex items-center justify-center w-6">
                      {rankIcon(i)}
                    </div>
                    <div className="flex-1 min-w-0">
                      <p className="text-sm font-medium truncate">
                        {entry.displayName}
                      </p>
                      <p className="text-xs text-muted-foreground">
                        {entry.gameName} &middot; {entry.gamesPlayed} games
                      </p>
                    </div>
                    <div className="flex items-center gap-2">
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
                  </div>
                ))}
              </div>
            )}
          </CardContent>
        </Card>

        {/* All-time leaderboard */}
        <Card>
          <CardHeader>
            <div className="flex items-center gap-2">
              <Trophy className="size-4 text-yellow-400" />
              <CardTitle>All-Time</CardTitle>
            </div>
            <CardDescription>Top {allTimeLimit} players overall</CardDescription>
          </CardHeader>
          <CardContent>
            {allTime.length === 0 ? (
              <div className="flex flex-col items-center gap-2 py-8 text-muted-foreground">
                <Users className="size-8" />
                <p className="text-sm">No players yet</p>
              </div>
            ) : (
              <div className="space-y-2">
                {allTime.map((entry, i) => (
                  <div
                    key={`${entry.displayName}-${i}`}
                    className="flex items-center gap-3 rounded-md bg-muted/50 px-3 py-2"
                  >
                    <div className="flex items-center justify-center w-6">
                      {rankIcon(i)}
                    </div>
                    <div className="flex-1 min-w-0">
                      <p className="text-sm font-medium truncate">
                        {entry.displayName}
                      </p>
                      <p className="text-xs text-muted-foreground">
                        {entry.gamesPlayed} games &middot; {entry.wins} wins
                      </p>
                    </div>
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
