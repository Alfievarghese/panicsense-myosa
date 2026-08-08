import { NextRequest, NextResponse } from "next/server";
import { randomUUID } from "crypto";

/*
 * In-memory episode store.
 * On Vercel serverless, this persists within a single cold-start lifecycle.
 * For a production deployment, replace with Vercel KV or Supabase.
 */
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

// Global store (survives across requests within same serverless instance)
const episodes: Episode[] = (globalThis as any).__panicsense_episodes ?? [];
(globalThis as any).__panicsense_episodes = episodes;

/**
 * POST /api/alert
 * Receives a panic episode alert from the ESP32 device.
 */
export async function POST(request: NextRequest) {
  try {
    const body = await request.json();

    const episode: Episode = {
      id: randomUUID(),
      team: body.team ?? "MANDI MASALA",
      event: body.event ?? "panic_episode",
      timestamp: body.timestamp ?? Math.floor(Date.now() / 1000),
      bpm_estimate: body.bpm_estimate ?? 0,
      tremor_duration_ms: body.tremor_duration_ms ?? 0,
      pressure_hpa: body.pressure_hpa ?? 0,
      temperature_c: body.temperature_c ?? 0,
      trigger: body.trigger ?? "unknown",
      received_at: new Date().toISOString(),
    };

    episodes.unshift(episode); // newest first

    // Cap at 100 episodes
    if (episodes.length > 100) {
      episodes.length = 100;
    }

    console.log(`[PanicSense] Episode received: ${episode.id} — BPM: ${episode.bpm_estimate}`);

    return NextResponse.json(
      { status: "ok", id: episode.id },
      {
        status: 200,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type",
        },
      }
    );
  } catch (error) {
    console.error("[PanicSense] Error processing alert:", error);
    return NextResponse.json(
      { status: "error", message: "Invalid JSON payload" },
      { status: 400 }
    );
  }
}

/**
 * GET /api/alert
 * Returns all stored episodes (newest first).
 */
export async function GET() {
  return NextResponse.json(
    { episodes, count: episodes.length },
    {
      status: 200,
      headers: {
        "Access-Control-Allow-Origin": "*",
      },
    }
  );
}

/**
 * OPTIONS /api/alert — CORS preflight
 */
export async function OPTIONS() {
  return new NextResponse(null, {
    status: 204,
    headers: {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
    },
  });
}
