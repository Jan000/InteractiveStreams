"use client";

import { useEffect, useState } from "react";
import Link from "next/link";
import { usePathname } from "next/navigation";
import {
  MonitorPlay,
  Radio,
  Settings,
  Gamepad2,
  Power,
  Trophy,
  Activity,
  BarChart3,
  Music,
  LogOut,
} from "lucide-react";
import { cn } from "@/lib/utils";
import { api } from "@/lib/api";
import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { ScrollArea } from "@/components/ui/scroll-area";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
  TooltipProvider,
} from "@/components/ui/tooltip";
import LoginPage from "@/app/login/page";

const nav = [
  { href: "/", label: "Streams", icon: MonitorPlay },
  { href: "/channels", label: "Channels", icon: Radio },
  { href: "/games", label: "Games", icon: Gamepad2 },
  { href: "/audio", label: "Audio", icon: Music },
  { href: "/statistics", label: "Statistics", icon: BarChart3 },
  { href: "/scoreboard", label: "Scoreboard", icon: Trophy },
  { href: "/performance", label: "Performance", icon: Activity },
  { href: "/settings", label: "Settings", icon: Settings },
];

export function AppShell({ children }: { children: React.ReactNode }) {
  const pathname = usePathname();
  const [authState, setAuthState] = useState<"loading" | "ok" | "login" | "setup">("loading");
  const [gitHash, setGitHash] = useState("");

  useEffect(() => {
    checkAuth();
    api.getStatus().then((s) => {
      if (s?.gitHash && s.gitHash !== "unknown") {
        setGitHash(s.gitHash);
      } else if (s?.version?.includes("+")) {
        setGitHash(s.version.split("+").pop() ?? "");
      }
    }).catch(() => {});
  }, []);

  const checkAuth = async () => {
    try {
      const status = await api.getAuthStatus();
      if (!status.passwordSet) {
        // Check if API key is the only auth – if so, user is already authed
        // (getAuthStatus returns authenticated=true when no auth is configured)
        if (status.authenticated) {
          setAuthState("ok");
        } else {
          setAuthState("setup");
        }
      } else if (status.authenticated) {
        setAuthState("ok");
      } else {
        setAuthState("login");
      }
    } catch {
      // Server unreachable or no auth configured – show dashboard
      setAuthState("ok");
    }
  };

  const handleLoginSuccess = (token: string) => {
    // Store token – the api.ts authHeaders already reads from localStorage
    localStorage.setItem("is_session_token", token);
    setAuthState("ok");
  };

  const handleLogout = async () => {
    try {
      await api.authLogout();
    } catch { /* ignore */ }
    localStorage.removeItem("is_session_token");
    setAuthState("login");
  };

  if (authState === "loading") {
    return (
      <div className="flex min-h-screen items-center justify-center bg-background">
        <div className="text-muted-foreground">Loading...</div>
      </div>
    );
  }

  if (authState === "login" || authState === "setup") {
    return <LoginPage isSetup={authState === "setup"} onSuccess={handleLoginSuccess} />;
  }

  return (
    <TooltipProvider>
      <div className="flex h-screen overflow-hidden bg-background">
        {/* Sidebar */}
        <aside className="flex h-full w-[220px] flex-col border-r border-border bg-card">
          {/* Logo / Title */}
          <div className="flex items-center gap-2 px-4 py-5">
            <Gamepad2 className="size-6 text-primary" />
            <span className="text-base font-semibold tracking-tight">
              InteractiveStreams
            </span>
          </div>

          <Separator />

          {/* Nav links */}
          <ScrollArea className="flex-1 px-2 py-3">
            <nav className="flex flex-col gap-1">
              {nav.map(({ href, label, icon: Icon }) => {
                const active =
                  href === "/" ? pathname === "/" : pathname.startsWith(href);
                return (
                  <Link
                    key={href}
                    href={href}
                    className={cn(
                      "flex items-center gap-3 rounded-md px-3 py-2 text-sm font-medium transition-colors",
                      active
                        ? "bg-accent text-accent-foreground"
                        : "text-muted-foreground hover:bg-accent/50 hover:text-foreground"
                    )}
                  >
                    <Icon className="size-4" />
                    {label}
                  </Link>
                );
              })}
            </nav>
          </ScrollArea>

          <Separator />

          {/* Footer */}
          <div className="flex items-center justify-between px-3 py-3">
            <span className="text-xs text-muted-foreground">{gitHash || ""}</span>
            <div className="flex gap-1">
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-7"
                    onClick={() => {
                      api.saveConfig().catch(console.error);
                    }}
                  >
                    <Settings className="size-3.5" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent side="top">Save Config</TooltipContent>
              </Tooltip>
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-7"
                    onClick={handleLogout}
                  >
                    <LogOut className="size-3.5" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent side="top">Logout</TooltipContent>
              </Tooltip>
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-7 text-destructive hover:text-destructive"
                    onClick={() => {
                      if (confirm("Shut down the server?")) {
                        api.shutdown().catch(console.error);
                      }
                    }}
                  >
                    <Power className="size-3.5" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent side="top">Shutdown Server</TooltipContent>
              </Tooltip>
            </div>
          </div>
        </aside>

        {/* Main content */}
        <main className="flex-1 overflow-auto p-6">{children}</main>
      </div>
    </TooltipProvider>
  );
}
