"use client";

import { useState, useEffect, useCallback } from "react";

interface Episode {
  id: string;
  team: string;
  event: string;
  timestamp: number;
  bpm_estimate: number;
  tremor_duration_ms: number;
  pressure_hpa: number;
  temperature_c: number;
  trigger: string;
  received_at: string;
}

function formatTime(epoch: number): string {
  const d = new Date(epoch * 1000);
  return d.toLocaleTimeString("en-US", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: true,
  });
}

function formatDate(epoch: number): string {
  const d = new Date(epoch * 1000);
  return d.toLocaleDateString("en-US", {
    month: "short",
    day: "numeric",
    year: "numeric",
  });
}

function timeAgo(isoStr: string): string {
  const diff = Date.now() - new Date(isoStr).getTime();
  const mins = Math.floor(diff / 60000);
  if (mins < 1) return "just now";
  if (mins < 60) return `${mins}m ago`;
  const hrs = Math.floor(mins / 60);
  if (hrs < 24) return `${hrs}h ago`;
  return `${Math.floor(hrs / 24)}d ago`;
}

/* ─── SVG ECG Line ─────────────────────────────────────── */
function ECGLine({ className = "" }: { className?: string }) {
  return (
    <svg viewBox="0 0 200 40" className={`w-full h-8 ${className}`} preserveAspectRatio="none">
      <path
        d="M0,20 L30,20 L35,20 L40,8 L45,32 L50,4 L55,36 L60,20 L70,20 L75,16 L80,20 L120,20 L125,20 L130,8 L135,32 L140,4 L145,36 L150,20 L160,20 L165,16 L170,20 L200,20"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.5"
        className="text-emerald-500"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

interface StatCardProps {
  label: string;
  value: string;
  unit?: string;
  accent?: boolean;
  delay?: number;
}

function StatCard({ label, value, unit, accent, delay }: StatCardProps) {
  return (
    <div
      className="bg-glass group relative overflow-hidden rounded-2xl px-5 py-5 transition-all duration-300 hover:-translate-y-1 hover:border-emerald-500/30 hover:shadow-lg hover:shadow-emerald-500/10 border border-zinc-800/60"
      style={{
        animation: `fade-in 0.6s ease-out ${delay}ms both`,
      }}
    >
      {accent && (
        <div className="absolute -right-4 -top-4 h-16 w-16 rounded-full bg-emerald-500/10 blur-2xl transition-all duration-500 group-hover:bg-emerald-500/20" />
      )}
      <p className="text-xs font-medium tracking-wide text-zinc-500 uppercase">
        {label}
      </p>
      <p className="mt-2 flex items-baseline gap-1.5">
        <span
          className={`font-mono text-3xl font-semibold tracking-tight ${
            accent ? "text-emerald-400" : "text-zinc-100"
          }`}
        >
          {value}
        </span>
        {unit && (
          <span className="text-sm font-medium text-zinc-500">{unit}</span>
        )}
      </p>
    </div>
  );
}

/* ─── Episode Row ──────────────────────────────────────── */
function EpisodeRow({
  episode,
  index,
}: {
  episode: Episode;
  index: number;
}) {
  const isElevated = episode.bpm_estimate > 100;

  return (
    <div
      className="group flex flex-col gap-4 rounded-xl bg-glass px-5 py-4 transition-all duration-300 hover:border-zinc-500/30 hover:bg-zinc-800/40 sm:flex-row sm:items-center border border-zinc-800/40"
      style={{
        animation: `fade-in 0.4s ease-out ${index * 80}ms both`,
      }}
    >
      {/* Pulse indicator */}
      <div className="relative flex-shrink-0">
        <div
          className={`h-2.5 w-2.5 rounded-full ${
            isElevated ? "bg-red-400" : "bg-emerald-400"
          }`}
        />
        {index === 0 && (
          <div
            className={`absolute inset-0 h-2.5 w-2.5 rounded-full animate-pulse-ring ${
              isElevated ? "bg-red-400" : "bg-emerald-400"
            }`}
          />
        )}
      </div>

      {/* Main content */}
      <div className="min-w-0 flex-1">
        <div className="flex items-center gap-2">
          <span className="text-sm font-medium text-zinc-200">
            Episode
          </span>
          <span
            className={`rounded-full px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wider ${
              episode.trigger === "manual"
                ? "bg-amber-500/10 text-amber-400"
                : "bg-emerald-500/10 text-emerald-400"
            }`}
          >
            {episode.trigger}
          </span>
        </div>
        <p className="mt-0.5 text-xs text-zinc-500">
          {formatDate(episode.timestamp)} at {formatTime(episode.timestamp)}
        </p>
      </div>

      {/* Metrics */}
      <div className="hidden gap-6 sm:flex">
        <div className="text-right">
          <p className="font-mono text-lg font-semibold text-zinc-100">
            {episode.bpm_estimate.toFixed(0)}
          </p>
          <p className="text-[10px] uppercase tracking-wider text-zinc-500">
            BPM
          </p>
        </div>
        <div className="text-right">
          <p className="font-mono text-lg font-semibold text-zinc-100">
            {(episode.tremor_duration_ms / 1000).toFixed(1)}
          </p>
          <p className="text-[10px] uppercase tracking-wider text-zinc-500">
            sec
          </p>
        </div>
        <div className="text-right">
          <p className="font-mono text-lg font-semibold text-zinc-100">
            {episode.temperature_c.toFixed(1)}
          </p>
          <p className="text-[10px] uppercase tracking-wider text-zinc-500">
            temp
          </p>
        </div>
      </div>

      {/* Time ago */}
      <span className="flex-shrink-0 text-xs text-zinc-600">
        {timeAgo(episode.received_at)}
      </span>
    </div>
  );
}

/* ─── Skeleton Loader ──────────────────────────────────── */
function SkeletonRow() {
  return (
    <div className="flex items-center gap-4 rounded-xl bg-glass px-5 py-4">
      <div className="h-2.5 w-2.5 rounded-full bg-zinc-800 animate-pulse" />
      <div className="flex-1 space-y-2">
        <div className="h-3 w-24 rounded bg-zinc-800 animate-pulse" />
        <div className="h-2 w-36 rounded bg-zinc-800/60 animate-pulse" />
      </div>
      <div className="h-4 w-12 rounded bg-zinc-800 animate-pulse" />
    </div>
  );
}

/* ─── Empty State ──────────────────────────────────────── */
function EmptyState() {
  return (
    <div className="animate-fade-in flex flex-col items-center justify-center py-16 text-center">
      <div className="relative mb-6">
        <svg
          width="64"
          height="64"
          viewBox="0 0 64 64"
          fill="none"
          className="text-zinc-700"
        >
          <circle cx="32" cy="32" r="30" stroke="currentColor" strokeWidth="2" />
          <path
            d="M20 32 L26 32 L29 24 L32 40 L35 28 L38 36 L41 32 L44 32"
            stroke="currentColor"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
            className="text-emerald-500/50"
          />
        </svg>
      </div>
      <h3 className="text-lg font-medium text-zinc-400">No episodes yet</h3>
      <p className="mt-1 max-w-xs text-sm text-zinc-600">
        Episodes from your PanicSense device will appear here in real-time when detected.
      </p>
    </div>
  );
}

/* ─── Main Dashboard ───────────────────────────────────── */
export default function Dashboard() {
  const [episodes, setEpisodes] = useState<Episode[]>([]);
  const [loading, setLoading] = useState(true);
  const [lastRefresh, setLastRefresh] = useState<Date>(new Date());

  const [mounted, setMounted] = useState(false);

  const fetchEpisodes = useCallback(async () => {
    try {
      const res = await fetch("/api/alert");
      const data = await res.json();
      setEpisodes(data.episodes || []);
    } catch {
      // Silently fail — will retry
    } finally {
      setLoading(false);
      setLastRefresh(new Date());
    }
  }, []);

  useEffect(() => {
    setMounted(true);
    fetchEpisodes();
    const interval = setInterval(fetchEpisodes, 5000); // Poll every 5s
    return () => clearInterval(interval);
  }, [fetchEpisodes]);

  // Compute stats
  const totalEpisodes = episodes.length;
  const avgBpm =
    totalEpisodes > 0
      ? episodes.reduce((s, e) => s + e.bpm_estimate, 0) / totalEpisodes
      : 0;
  const lastEpisode = episodes[0];
  const autoCount = episodes.filter((e) => e.trigger === "auto").length;

  // Prevent hydration mismatch
  if (!mounted) return null;

  return (
    <div className="mx-auto max-w-5xl px-4 py-8 sm:px-6 lg:px-8">
      {/* ─── Header ─────────────────────────────────────── */}
      <header className="animate-fade-in mb-10">
        <div className="flex items-start justify-between">
          <div>
            <h1 className="text-3xl font-bold tracking-tight text-zinc-50 sm:text-4xl">
              PanicSense
            </h1>
            <p className="mt-1 text-sm text-zinc-500">
              Real-time panic episode monitor
            </p>
          </div>
          <div className="flex items-center gap-3">
            <div className="flex items-center gap-1.5 rounded-full bg-emerald-500/10 px-2 py-1 border border-emerald-500/20">
              <div className="h-1.5 w-1.5 rounded-full bg-emerald-400 animate-pulse-ring relative" />
              <div className="h-1.5 w-1.5 rounded-full bg-emerald-400 absolute" />
              <span className="text-xs font-semibold text-emerald-400 tracking-wider uppercase ml-1">Live</span>
            </div>
            <span className="rounded-full bg-glass px-3 py-1 text-xs font-medium text-zinc-300 border border-white/10 shadow-sm backdrop-blur-md">
              MANDI MASALA
            </span>
          </div>
        </div>

        {/* ECG decoration */}
        <div className="mt-4 overflow-hidden opacity-30">
          <ECGLine />
        </div>
      </header>

      {/* ─── Stats Grid ─────────────────────────────────── */}
      <div className="mb-8 grid grid-cols-2 gap-3 lg:grid-cols-4">
        <StatCard
          label="Total Episodes"
          value={totalEpisodes.toString()}
          delay={0}
        />
        <StatCard
          label="Avg Heart Rate"
          value={avgBpm > 0 ? avgBpm.toFixed(0) : "--"}
          unit="BPM"
          accent
          delay={80}
        />
        <StatCard
          label="Auto Detected"
          value={autoCount.toString()}
          delay={160}
        />
        <StatCard
          label="Last Episode"
          value={lastEpisode ? timeAgo(lastEpisode.received_at) : "--"}
          delay={240}
        />
      </div>

      {/* ─── Episode Feed ───────────────────────────────── */}
      <section>
        <div className="mb-4 flex items-center justify-between">
          <h2 className="text-lg font-semibold tracking-tight text-zinc-200">
            Episode Feed
          </h2>
          <button
            onClick={fetchEpisodes}
            className="rounded-lg bg-glass px-4 py-1.5 text-xs font-medium text-zinc-300 transition-all duration-300 hover:border-zinc-500/50 hover:bg-zinc-800/50 hover:text-white active:scale-[0.97] hover:shadow-lg backdrop-blur-md"
          >
            Refresh
          </button>
        </div>

        <div className="space-y-2">
          {loading ? (
            <>
              <SkeletonRow />
              <SkeletonRow />
              <SkeletonRow />
            </>
          ) : episodes.length === 0 ? (
            <EmptyState />
          ) : (
            episodes.map((ep, i) => (
              <EpisodeRow key={ep.id} episode={ep} index={i} />
            ))
          )}
        </div>
      </section>

      {/* ─── Footer ─────────────────────────────────────── */}
      <footer className="mt-12 border-t border-zinc-800/40 pt-6">
        <div className="flex flex-col items-center gap-2 text-center sm:flex-row sm:justify-between sm:text-left">
          <div className="group">
            <p className="text-sm font-medium text-zinc-100 group-hover:text-emerald-400 transition-colors">
              PanicSense by Team MANDI MASALA
            </p>
            <p className="text-xs text-zinc-500">
              IEEE MYOSA International Event 6.0
            </p>
          </div>
          <p className="text-[10px] text-zinc-700">
            Last updated: {lastRefresh.toLocaleTimeString()}
          </p>
        </div>
      </footer>
    </div>
  );
}
