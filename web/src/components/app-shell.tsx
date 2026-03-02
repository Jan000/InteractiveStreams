"use client";

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
  Layers,
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

const nav = [
  { href: "/", label: "Streams", icon: MonitorPlay },
  { href: "/channels", label: "Channels", icon: Radio },
  { href: "/profiles", label: "Profiles", icon: Layers },
  { href: "/audio", label: "Audio", icon: Music },
  { href: "/statistics", label: "Statistics", icon: BarChart3 },
  { href: "/scoreboard", label: "Scoreboard", icon: Trophy },
  { href: "/performance", label: "Performance", icon: Activity },
  { href: "/settings", label: "Settings", icon: Settings },
];

export function AppShell({ children }: { children: React.ReactNode }) {
  const pathname = usePathname();

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
            <span className="text-xs text-muted-foreground">v0.2.0</span>
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
