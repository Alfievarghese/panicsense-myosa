import type { Metadata } from "next";
import { Analytics } from "@vercel/analytics/react";
import "./globals.css";

export const metadata: Metadata = {
  title: "PanicSense Dashboard — MANDI MASALA",
  description:
    "Real-time panic episode monitoring dashboard for the PanicSense wearable device. IEEE MYOSA International Event 6.0.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className="dark">
      <head>
        <link rel="preconnect" href="https://fonts.googleapis.com" />
        <link
          rel="preconnect"
          href="https://fonts.gstatic.com"
          crossOrigin="anonymous"
        />
        <link
          href="https://fonts.googleapis.com/css2?family=Geist:wght@300;400;500;600;700&family=Geist+Mono:wght@400;500&display=swap"
          rel="stylesheet"
        />
      </head>
      <body className="min-h-screen bg-transparent text-zinc-100 antialiased">
        <div className="ambient-background">
          <div className="blob-1"></div>
          <div className="blob-2"></div>
        </div>
        {children}
        <Analytics />
      </body>
    </html>
  );
}
