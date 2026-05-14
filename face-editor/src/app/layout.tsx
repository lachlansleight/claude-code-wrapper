import "./globals.css";
import { Inter } from "next/font/google";

const inter = Inter({ subsets: ["latin"] });

export const metadata = {
  title: "Face editor — v3 simulator",
  description: "Browser preview of robot_v3 face renderer and frame controller",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en">
      <body className={`${inter.className} antialiased bg-face-bg`}>
        {children}
      </body>
    </html>
  );
}
