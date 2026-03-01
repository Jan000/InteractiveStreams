"use client";

import { useEffect, useState, useCallback } from "react";
import {
  api,
  type ChannelStatsEntry,
  type StreamStatsEntry,
  type StatsUser,
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
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Users,
  MessageSquare,
  Ratio,
  Timer,
  Radio,
  MonitorPlay,
  RotateCcw,
  Clock,
  TrendingUp,
} from "lucide-react";
import { toast } from "sonner";

// ── Helpers ──────────────────────────────────────────────────────────────────

function formatDuration(seconds: number): string {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  if (seconds < 3600) {
    const m = Math.floor(seconds / 60);
    const s = Math.round(seconds % 60);
    return `${m}m ${s}s`;
  }
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  return `${h}h ${m}m`;
}

function formatUptime(seconds: number): string {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
  if (seconds < 86400) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return `${h}h ${m}m`;
  }
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  return `${d}d ${h}h`;
}

// ── Stat cards ───────────────────────────────────────────────────────────────

function StatCard({
  icon: Icon,
  label,
  value,
  unit,
  description,
}: {
  icon: React.ElementType;
  label: string;
  value: string | number;
  unit?: string;
  description?: string;
}) {
  return (
    <Card>
      <CardContent className="flex items-center gap-3 p-4">
        <div className="rounded-md p-2 bg-primary/10">
          <Icon className="size-5 text-primary" />
        </div>
        <div className="min-w-0">
          <p className="text-xs text-muted-foreground">{label}</p>
          <p className="text-lg font-bold">
            {value}
            {unit && (
              <span className="text-xs font-normal text-muted-foreground ml-1">
                {unit}
              </span>
            )}
          </p>
          {description && (
            <p className="text-[10px] text-muted-foreground truncate">
              {description}
            </p>
          )}
        </div>
      </CardContent>
    </Card>
  );
}

// ── User table ───────────────────────────────────────────────────────────────

function UserTable({ users }: { users: StatsUser[] }) {
  if (users.length === 0) {
    return (
      <p className="text-sm text-muted-foreground text-center py-4">
        No user data yet
      </p>
    );
  }

  const top = users.slice(0, 15);

  return (
    <div className="overflow-x-auto">
      <table className="w-full text-sm">
        <thead>
          <tr className="border-b text-muted-foreground text-xs">
            <th className="text-left py-2 px-2">#</th>
            <th className="text-left py-2 px-2">User</th>
            <th className="text-right py-2 px-2">Msgs</th>
            <th className="text-right py-2 px-2">Sessions</th>
            <th className="text-right py-2 px-2">Engagement</th>
          </tr>
        </thead>
        <tbody>
          {top.map((user, i) => (
            <tr
              key={user.userId}
              className="border-b last:border-0 hover:bg-muted/50"
            >
              <td className="py-1.5 px-2 text-muted-foreground">{i + 1}</td>
              <td className="py-1.5 px-2 font-medium truncate max-w-[160px]">
                {user.displayName}
              </td>
              <td className="py-1.5 px-2 text-right tabular-nums">
                {user.messages}
              </td>
              <td className="py-1.5 px-2 text-right tabular-nums">
                {user.sessions}
              </td>
              <td className="py-1.5 px-2 text-right tabular-nums text-muted-foreground">
                {formatDuration(user.engagementSeconds)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      {users.length > 15 && (
        <p className="text-xs text-muted-foreground text-center pt-2">
          … and {users.length - 15} more users
        </p>
      )}
    </div>
  );
}

// ── Stats section for a single entity (stream or channel) ────────────────────

function StatsSection({
  title,
  subtitle,
  badge,
  stats,
  users,
  onReset,
}: {
  title: string;
  subtitle?: string;
  badge?: { label: string; variant?: "default" | "secondary" | "outline" };
  stats: {
    uniqueViewers: number;
    totalInteractions: number;
    interactionRatio: number;
    avgEngagementSeconds: number;
    medianEngagementSeconds: number;
    totalSessions: number;
    uptimeSeconds: number;
  };
  users: StatsUser[];
  onReset: () => void;
}) {
  return (
    <Card>
      <CardHeader className="pb-3">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <CardTitle className="text-base">{title}</CardTitle>
            {badge && (
              <Badge variant={badge.variant ?? "secondary"}>
                {badge.label}
              </Badge>
            )}
          </div>
          <div className="flex items-center gap-2">
            <Badge variant="outline" className="text-xs">
              <Clock className="size-3 mr-1" />
              {formatUptime(stats.uptimeSeconds)}
            </Badge>
            <Button
              variant="ghost"
              size="icon"
              className="size-7"
              onClick={onReset}
              title="Reset statistics"
            >
              <RotateCcw className="size-3.5" />
            </Button>
          </div>
        </div>
        {subtitle && (
          <CardDescription className="text-xs">{subtitle}</CardDescription>
        )}
      </CardHeader>
      <CardContent className="space-y-4">
        {/* Summary stats grid */}
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 lg:grid-cols-6">
          <StatCard
            icon={Users}
            label="Unique Viewers"
            value={stats.uniqueViewers}
          />
          <StatCard
            icon={MessageSquare}
            label="Interactions"
            value={stats.totalInteractions}
          />
          <StatCard
            icon={Ratio}
            label="Msgs/Viewer"
            value={stats.interactionRatio.toFixed(1)}
          />
          <StatCard
            icon={Timer}
            label="Avg. Engagement"
            value={formatDuration(stats.avgEngagementSeconds)}
            description="Time between first & last msg"
          />
          <StatCard
            icon={Timer}
            label="Median Engagement"
            value={formatDuration(stats.medianEngagementSeconds)}
          />
          <StatCard
            icon={TrendingUp}
            label="Total Sessions"
            value={stats.totalSessions}
          />
        </div>

        {/* User breakdown */}
        <Separator />
        <div>
          <h4 className="text-sm font-medium mb-2">
            Top Users
            <span className="text-muted-foreground font-normal ml-1">
              ({users.length} total)
            </span>
          </h4>
          <UserTable users={users} />
        </div>
      </CardContent>
    </Card>
  );
}

// ── Main page ────────────────────────────────────────────────────────────────

export default function StatisticsPage() {
  const [channelStats, setChannelStats] = useState<ChannelStatsEntry[] | null>(
    null
  );
  const [streamStats, setStreamStats] = useState<StreamStatsEntry[] | null>(
    null
  );
  const [error, setError] = useState<string | null>(null);

  const fetchData = useCallback(async () => {
    try {
      const [ch, st] = await Promise.all([
        api.getChannelStats(),
        api.getStreamStats(),
      ]);
      setChannelStats(ch);
      setStreamStats(st);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Connection lost");
    }
  }, []);

  useEffect(() => {
    let active = true;
    const poll = async () => {
      while (active) {
        await fetchData();
        await new Promise((r) => setTimeout(r, 2000));
      }
    };
    poll();
    return () => {
      active = false;
    };
  }, [fetchData]);

  const handleResetAll = async () => {
    try {
      await api.resetAllStats();
      toast.success("All statistics reset");
      fetchData();
    } catch {
      toast.error("Failed to reset statistics");
    }
  };

  const handleResetChannel = async (channelId: string) => {
    try {
      await api.resetChannelStats(channelId);
      toast.success("Channel statistics reset");
      fetchData();
    } catch {
      toast.error("Failed to reset");
    }
  };

  const handleResetStream = async (streamId: string) => {
    try {
      await api.resetStreamStats(streamId);
      toast.success("Stream statistics reset");
      fetchData();
    } catch {
      toast.error("Failed to reset");
    }
  };

  // Loading state
  if (!channelStats && !streamStats && !error) {
    return (
      <div className="space-y-4">
        <Skeleton className="h-8 w-64" />
        <div className="grid gap-4">
          {[1, 2, 3].map((i) => (
            <Skeleton key={i} className="h-64 w-full" />
          ))}
        </div>
      </div>
    );
  }

  // Aggregate totals across all streams
  const totalViewers =
    streamStats?.reduce((sum, s) => sum + s.stats.uniqueViewers, 0) ?? 0;
  const totalInteractions =
    streamStats?.reduce((sum, s) => sum + s.stats.totalInteractions, 0) ?? 0;

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold tracking-tight">Statistics</h1>
          <p className="text-sm text-muted-foreground">
            Per-stream and per-channel viewer interactions and engagement
          </p>
        </div>
        <Button variant="outline" size="sm" onClick={handleResetAll}>
          <RotateCcw className="size-4 mr-2" />
          Reset All
        </Button>
      </div>

      {error && (
        <Card className="border-destructive">
          <CardContent className="p-4 text-sm text-destructive">
            {error}
          </CardContent>
        </Card>
      )}

      {/* Global summary */}
      <div className="grid gap-3 grid-cols-2 sm:grid-cols-4">
        <StatCard icon={Users} label="Total Viewers" value={totalViewers} />
        <StatCard
          icon={MessageSquare}
          label="Total Interactions"
          value={totalInteractions}
        />
        <StatCard
          icon={MonitorPlay}
          label="Streams"
          value={streamStats?.length ?? 0}
        />
        <StatCard
          icon={Radio}
          label="Channels"
          value={channelStats?.length ?? 0}
        />
      </div>

      {/* Per-Stream Statistics */}
      <div className="space-y-4">
        <h2 className="text-lg font-semibold flex items-center gap-2">
          <MonitorPlay className="size-5" />
          Stream Statistics
        </h2>
        {streamStats && streamStats.length > 0 ? (
          streamStats.map((s) => (
            <StatsSection
              key={s.streamId}
              title={s.streamName}
              subtitle={`Stream ID: ${s.streamId}`}
              badge={{ label: "Stream", variant: "default" }}
              stats={s.stats}
              users={s.stats.users}
              onReset={() => handleResetStream(s.streamId)}
            />
          ))
        ) : (
          <Card>
            <CardContent className="p-6 text-center text-muted-foreground">
              No streams configured
            </CardContent>
          </Card>
        )}
      </div>

      <Separator />

      {/* Per-Channel Statistics */}
      <div className="space-y-4">
        <h2 className="text-lg font-semibold flex items-center gap-2">
          <Radio className="size-5" />
          Channel Statistics
        </h2>
        {channelStats && channelStats.length > 0 ? (
          channelStats.map((ch) => (
            <StatsSection
              key={ch.channelId}
              title={ch.channelName}
              subtitle={`${ch.platform} · ${ch.channelId}`}
              badge={{
                label: ch.connected ? "Connected" : "Disconnected",
                variant: ch.connected ? "default" : "outline",
              }}
              stats={ch.stats}
              users={ch.stats.users}
              onReset={() => handleResetChannel(ch.channelId)}
            />
          ))
        ) : (
          <Card>
            <CardContent className="p-6 text-center text-muted-foreground">
              No channels configured
            </CardContent>
          </Card>
        )}
      </div>
    </div>
  );
}
