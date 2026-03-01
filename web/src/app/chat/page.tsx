"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { Send, Swords, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, UserPlus } from "lucide-react";
import { api } from "@/lib/api";
import { useStatus } from "@/hooks/use-status";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Separator } from "@/components/ui/separator";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { toast } from "sonner";

const quickCommands = [
  { label: "Join", cmd: "!join", icon: UserPlus, color: "bg-green-600 hover:bg-green-700" },
  { label: "Left", cmd: "!left", icon: ArrowLeft },
  { label: "Right", cmd: "!right", icon: ArrowRight },
  { label: "Jump", cmd: "!jump", icon: ArrowUp },
  { label: "Down", cmd: "!down", icon: ArrowDown },
  { label: "Attack", cmd: "!attack", icon: Swords, color: "bg-red-600 hover:bg-red-700" },
  { label: "Special", cmd: "!special" },
  { label: "Dash", cmd: "!dash" },
  { label: "Block", cmd: "!block" },
];

const PLAYERS = ["Player1", "Player2", "Player3", "Player4"];

export default function ChatPage() {
  const { data } = useStatus(2000);
  const [username, setUsername] = useState("Player1");
  const [message, setMessage] = useState("");
  const [log, setLog] = useState<string[]>([]);
  const [autoPlay, setAutoPlay] = useState(false);
  const autoPlayRef = useRef(false);
  const logEndRef = useRef<HTMLDivElement>(null);

  // Poll chat log
  useEffect(() => {
    const interval = setInterval(async () => {
      try {
        const entries = await api.getChatLog();
        setLog(entries);
      } catch {
        // ignore
      }
    }, 1500);
    return () => clearInterval(interval);
  }, []);

  // Auto-scroll log
  useEffect(() => {
    logEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [log]);

  const sendCmd = useCallback(
    async (user: string, text: string) => {
      try {
        await api.sendChat(user, text);
      } catch (e) {
        toast.error(e instanceof Error ? e.message : "Failed");
      }
    },
    []
  );

  const handleSend = () => {
    if (!message.trim()) return;
    sendCmd(username, message.trim());
    setMessage("");
  };

  // Auto-play: random commands from random players
  useEffect(() => {
    autoPlayRef.current = autoPlay;
    if (!autoPlay) return;

    const cmds = ["!left", "!right", "!jump", "!attack", "!special", "!dash", "!block"];
    const interval = setInterval(() => {
      if (!autoPlayRef.current) return;
      const user = PLAYERS[Math.floor(Math.random() * PLAYERS.length)];
      const cmd = cmds[Math.floor(Math.random() * cmds.length)];
      sendCmd(user, cmd);
    }, 300);

    return () => clearInterval(interval);
  }, [autoPlay, sendCmd]);

  // Current game commands info
  const activeStream = data?.streams[0];
  const gameCommands = activeStream?.game?.commands;

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-bold tracking-tight">Chat Test</h1>
        <p className="text-sm text-muted-foreground">
          Send commands to the game as if you were a chat viewer
        </p>
      </div>

      <Separator />

      <div className="grid gap-6 lg:grid-cols-[1fr_320px]">
        {/* Left: Controls */}
        <div className="space-y-6">
          {/* Player tabs */}
          <Tabs value={username} onValueChange={setUsername}>
            <TabsList>
              {PLAYERS.map((p) => (
                <TabsTrigger key={p} value={p}>
                  {p}
                </TabsTrigger>
              ))}
            </TabsList>
            <TabsContent value={username} className="mt-3">
              <p className="text-xs text-muted-foreground">
                Sending commands as <span className="font-bold text-foreground">{username}</span>
              </p>
            </TabsContent>
          </Tabs>

          {/* Quick commands */}
          <Card>
            <CardHeader className="pb-3">
              <CardTitle className="text-sm">Quick Commands</CardTitle>
            </CardHeader>
            <CardContent>
              <div className="flex flex-wrap gap-2">
                {quickCommands.map(({ label, cmd, icon: Icon, color }) => (
                  <Button
                    key={cmd}
                    size="sm"
                    variant="secondary"
                    className={`gap-1.5 ${color ?? ""}`}
                    onClick={() => sendCmd(username, cmd)}
                  >
                    {Icon && <Icon className="size-3.5" />}
                    {label}
                  </Button>
                ))}
              </div>
            </CardContent>
          </Card>

          {/* Free-form input */}
          <div className="flex gap-2">
            <Input
              placeholder="Type a command (e.g. !join, !attack)…"
              value={message}
              onChange={(e) => setMessage(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && handleSend()}
            />
            <Button onClick={handleSend} className="gap-1">
              <Send className="size-4" />
              Send
            </Button>
          </div>

          {/* Auto-play toggle */}
          <div className="flex items-center gap-3">
            <Button
              variant={autoPlay ? "destructive" : "outline"}
              size="sm"
              onClick={() => setAutoPlay(!autoPlay)}
            >
              {autoPlay ? "Stop Auto-Play" : "Start Auto-Play"}
            </Button>
            <p className="text-xs text-muted-foreground">
              Sends random commands from random players every 300ms
            </p>
          </div>

          {/* Game commands reference */}
          {gameCommands && Object.keys(gameCommands).length > 0 && (
            <Card>
              <CardHeader className="pb-3">
                <CardTitle className="text-sm">
                  Available Commands — {activeStream?.game?.name}
                </CardTitle>
              </CardHeader>
              <CardContent>
                <pre className="max-h-48 overflow-auto rounded bg-muted p-3 text-xs">
                  {JSON.stringify(gameCommands, null, 2)}
                </pre>
              </CardContent>
            </Card>
          )}
        </div>

        {/* Right: Chat log */}
        <Card className="h-fit">
          <CardHeader className="pb-3">
            <CardTitle className="text-sm">
              Chat Log ({log.length} messages)
            </CardTitle>
          </CardHeader>
          <CardContent>
            <ScrollArea className="h-[480px]">
              <div className="space-y-1 font-mono text-xs">
                {log.length === 0 && (
                  <p className="text-muted-foreground">No messages yet.</p>
                )}
                {log.map((entry, i) => (
                  <p key={i} className="break-all leading-relaxed">
                    {entry}
                  </p>
                ))}
                <div ref={logEndRef} />
              </div>
            </ScrollArea>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
