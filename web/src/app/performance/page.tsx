"use client";

import { useEffect, useState, useCallback } from "react";
import { api, type PerfSample, type PerfData } from "@/lib/api";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Activity,
  Cpu,
  HardDrive,
  MonitorPlay,
  Radio,
  Users,
  Timer,
} from "lucide-react";
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  Area,
  AreaChart,
} from "recharts";

function StatCard({
  icon: Icon,
  label,
  value,
  unit,
  color,
}: {
  icon: React.ElementType;
  label: string;
  value: string | number;
  unit?: string;
  color?: string;
}) {
  return (
    <Card>
      <CardContent className="flex items-center gap-3 p-4">
        <div className={`rounded-md p-2 bg-${color ?? "primary"}/10`}>
          <Icon className={`size-5 text-${color ?? "primary"}`} />
        </div>
        <div>
          <p className="text-xs text-muted-foreground">{label}</p>
          <p className="text-lg font-bold">
            {value}
            {unit && (
              <span className="text-xs font-normal text-muted-foreground ml-1">
                {unit}
              </span>
            )}
          </p>
        </div>
      </CardContent>
    </Card>
  );
}

const TIME_RANGES = [
  { value: "60", label: "1 minute" },
  { value: "300", label: "5 minutes" },
  { value: "600", label: "10 minutes" },
  { value: "1800", label: "30 minutes" },
  { value: "3600", label: "1 hour" },
];

export default function PerformancePage() {
  const [perf, setPerf] = useState<PerfData | null>(null);
  const [history, setHistory] = useState<PerfSample[]>([]);
  const [range, setRange] = useState("300");

  const fetchData = useCallback(async () => {
    try {
      const [p, h] = await Promise.all([
        api.getPerf(60),
        api.getPerfHistory(Number(range)),
      ]);
      setPerf(p);
      setHistory(h.samples ?? []);
    } catch {
      /* backend not running */
    }
  }, [range]);

  useEffect(() => {
    fetchData();
    const iv = setInterval(fetchData, 2000);
    return () => clearInterval(iv);
  }, [fetchData]);

  // Prepare chart data with relative time labels
  const chartData = history.map((s, i) => ({
    ...s,
    label: i,
    timeLabel:
      history.length > 0
        ? `-${Math.round(history[history.length - 1].time - s.time)}s`
        : "",
  }));

  return (
    <div className="flex flex-col gap-6 p-6">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Activity className="size-6 text-primary" />
          <div>
            <h1 className="text-2xl font-bold">Performance</h1>
            <p className="text-sm text-muted-foreground">
              Live system metrics and resource usage
            </p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <Label className="text-xs text-muted-foreground">Time range:</Label>
          <Select value={range} onValueChange={setRange}>
            <SelectTrigger className="w-32 h-8 text-sm">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {TIME_RANGES.map((r) => (
                <SelectItem key={r.value} value={r.value}>
                  {r.label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
      </div>

      {/* Summary cards */}
      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
        <StatCard
          icon={Timer}
          label="Avg FPS"
          value={perf?.avgFps != null ? perf.avgFps.toFixed(1) : "—"}
          color="green-500"
        />
        <StatCard
          icon={Cpu}
          label="Avg Frame Time"
          value={perf?.avgFrameTime != null ? perf.avgFrameTime.toFixed(2) : "—"}
          unit="ms"
          color="blue-500"
        />
        <StatCard
          icon={HardDrive}
          label="Memory"
          value={perf?.avgMemory != null ? perf.avgMemory.toFixed(1) : "—"}
          unit={`MB (peak: ${perf?.peakMemory != null ? perf.peakMemory.toFixed(0) : "—"})`}
          color="purple-500"
        />
        <StatCard
          icon={Users}
          label="Players"
          value={perf?.totalPlayers ?? "—"}
          unit={`across ${perf?.activeStreams ?? 0} streams`}
          color="orange-500"
        />
      </div>

      {/* FPS Chart */}
      <Card>
        <CardHeader className="pb-2">
          <div className="flex items-center gap-2">
            <Activity className="size-4 text-green-500" />
            <CardTitle className="text-sm">Frames per Second</CardTitle>
          </div>
          <CardDescription>Target: 60 FPS</CardDescription>
        </CardHeader>
        <CardContent>
          <ResponsiveContainer width="100%" height={200}>
            <AreaChart data={chartData}>
              <defs>
                <linearGradient id="fpsGrad" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#22c55e" stopOpacity={0.3} />
                  <stop offset="95%" stopColor="#22c55e" stopOpacity={0} />
                </linearGradient>
              </defs>
                <CartesianGrid strokeDasharray="3 3" stroke="#333" />
                <XAxis
                  dataKey="timeLabel"
                  tick={{ fontSize: 10, fill: "#888" }}
                  interval="preserveStartEnd"
                />
                <YAxis
                  domain={[0, "auto"]}
                  tick={{ fontSize: 10, fill: "#888" }}
                />
                <Tooltip
                  contentStyle={{
                    background: "#1a1a2e",
                    border: "1px solid #333",
                    borderRadius: 8,
                    fontSize: 12,
                  }}
                />
              <Area
                type="monotone"
                dataKey="fps"
                stroke="#22c55e"
                fill="url(#fpsGrad)"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
            </AreaChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>

      {/* Frame Time + Memory Chart */}
      <div className="grid gap-6 lg:grid-cols-2">
        <Card>
          <CardHeader className="pb-2">
            <div className="flex items-center gap-2">
              <Cpu className="size-4 text-blue-500" />
              <CardTitle className="text-sm">Frame Time</CardTitle>
            </div>
            <CardDescription>Lower is better (target: ~16.7ms)</CardDescription>
          </CardHeader>
          <CardContent>
            <ResponsiveContainer width="100%" height={180}>
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" stroke="#333" />
                <XAxis
                  dataKey="timeLabel"
                  tick={{ fontSize: 10, fill: "#888" }}
                  interval="preserveStartEnd"
                />
                <YAxis tick={{ fontSize: 10, fill: "#888" }} />
                <Tooltip
                  contentStyle={{
                    background: "#1a1a2e",
                    border: "1px solid #333",
                    borderRadius: 8,
                    fontSize: 12,
                  }}
                />
                <Line
                  type="monotone"
                  dataKey="frameTimeMs"
                  stroke="#3b82f6"
                  strokeWidth={2}
                  dot={false}
                  name="Frame Time (ms)"
                  isAnimationActive={false}
                />
              </LineChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="pb-2">
            <div className="flex items-center gap-2">
              <HardDrive className="size-4 text-purple-500" />
              <CardTitle className="text-sm">Memory Usage</CardTitle>
            </div>
            <CardDescription>Process working set</CardDescription>
          </CardHeader>
          <CardContent>
            <ResponsiveContainer width="100%" height={180}>
              <AreaChart data={chartData}>
                <defs>
                  <linearGradient id="memGrad" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="#a855f7" stopOpacity={0.3} />
                    <stop offset="95%" stopColor="#a855f7" stopOpacity={0} />
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" stroke="#333" />
                <XAxis
                  dataKey="timeLabel"
                  tick={{ fontSize: 10, fill: "#888" }}
                  interval="preserveStartEnd"
                />
                <YAxis tick={{ fontSize: 10, fill: "#888" }} />
                <Tooltip
                  contentStyle={{
                    background: "#1a1a2e",
                    border: "1px solid #333",
                    borderRadius: 8,
                    fontSize: 12,
                  }}
                />
                <Area
                  type="monotone"
                  dataKey="memoryMB"
                  stroke="#a855f7"
                  fill="url(#memGrad)"
                  strokeWidth={2}
                  dot={false}
                  name="Memory (MB)"
                  isAnimationActive={false}
                />
              </AreaChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>
      </div>

      {/* Players + Streams Chart */}
      <Card>
        <CardHeader className="pb-2">
          <div className="flex items-center gap-2">
            <Users className="size-4 text-orange-500" />
            <CardTitle className="text-sm">Active Players &amp; Streams</CardTitle>
          </div>
        </CardHeader>
        <CardContent>
          <ResponsiveContainer width="100%" height={180}>
            <LineChart data={chartData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#333" />
              <XAxis
                dataKey="timeLabel"
                tick={{ fontSize: 10, fill: "#888" }}
                interval="preserveStartEnd"
              />
              <YAxis tick={{ fontSize: 10, fill: "#888" }} />
              <Tooltip
                contentStyle={{
                  background: "#1a1a2e",
                  border: "1px solid #333",
                  borderRadius: 8,
                  fontSize: 12,
                }}
              />
              <Line
                type="monotone"
                dataKey="totalPlayers"
                stroke="#f97316"
                strokeWidth={2}
                dot={false}
                name="Players"
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="activeStreams"
                stroke="#06b6d4"
                strokeWidth={2}
                dot={false}
                name="Streams"
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="activeChannels"
                stroke="#84cc16"
                strokeWidth={2}
                dot={false}
                name="Channels"
                isAnimationActive={false}
              />
            </LineChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>

      <p className="text-xs text-muted-foreground text-center">
        Data refreshes every 2 seconds &middot; Samples are recorded every 5 frames
      </p>
    </div>
  );
}
