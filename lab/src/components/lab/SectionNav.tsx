import { useEffect, useState } from "react";
import { Github, Linkedin, Globe, Sun, Moon } from "lucide-react";
import { useTheme } from "@/hooks/use-theme";

const ITEMS = [
  { id: "demo", label: "cube" },
  { id: "backstory", label: "backstory" },
  { id: "how-it-works", label: "how it works" },
  { id: "metrics", label: "numbers" },
  { id: "histogram", label: "histogram" },
  { id: "results", label: "results" },
  { id: "decisions", label: "decisions" },
  { id: "notes", label: "limits & build" },
];

export function SectionNav() {
  const [active, setActive] = useState<string>(ITEMS[0].id);
  const { theme, toggle } = useTheme();

  useEffect(() => {
    const visible = new Map<string, IntersectionObserverEntry>();
    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) visible.set(entry.target.id, entry);
          else visible.delete(entry.target.id);
        });
        const topmost = Array.from(visible.values()).sort(
          (a, b) => a.boundingClientRect.top - b.boundingClientRect.top
        )[0];
        if (topmost) setActive(topmost.target.id);
      },
      { rootMargin: "0px 0px -55% 0px", threshold: 0 },
    );
    ITEMS.forEach(({ id }) => {
      const el = document.getElementById(id);
      if (el) observer.observe(el);
    });
    return () => observer.disconnect();
  }, []);

  return (
    <nav
      aria-label="Sections"
      className="sticky top-0 z-30 -mx-6 mb-2 border-b border-hairline bg-background/85 px-6 backdrop-blur sm:-mx-8 sm:px-8"
    >
      <div className="flex items-center justify-between gap-4">
        <ul className="flex gap-1 overflow-x-auto py-2 font-mono text-xs">
          {ITEMS.map((item) => (
            <li key={item.id}>
              <a
                href={`#${item.id}`}
                aria-current={active === item.id ? "true" : undefined}
                className={`block whitespace-nowrap rounded-full px-3 py-1 transition-colors ${
                  active === item.id
                    ? "bg-metric-1/15 text-metric-1"
                    : "text-muted-foreground hover:text-foreground"
                }`}
              >
                {item.label}
              </a>
            </li>
          ))}
        </ul>
        <div className="flex items-center gap-1 border-l border-hairline pl-4">
          <a
            href="https://github.com/tiffany-mares/OpenGL-Renderer"
            aria-label="GitHub"
            className="rounded-full p-2 text-muted-foreground transition-colors hover:text-metric-1"
          >
            <Github size={16} strokeWidth={1.5} />
          </a>
          <a
            href="https://www.linkedin.com/in/tiffany-mares"
            aria-label="LinkedIn"
            className="rounded-full p-2 text-muted-foreground transition-colors hover:text-metric-2"
          >
            <Linkedin size={16} strokeWidth={1.5} />
          </a>
          <a
            href="https://tiffanymares.com/"
            aria-label="Contact"
            className="rounded-full p-2 text-muted-foreground transition-colors hover:text-metric-3"
          >
            <Globe size={16} strokeWidth={1.5} />
          </a>
          <button
            type="button"
            onClick={toggle}
            aria-label={theme === "dark" ? "Switch to light mode" : "Switch to dark mode"}
            className="ml-1 rounded-full border border-hairline p-2 text-muted-foreground transition-colors hover:text-metric-3"
          >
            {theme === "dark" ? <Sun size={16} strokeWidth={1.5} /> : <Moon size={16} strokeWidth={1.5} />}
          </button>
        </div>
      </div>
    </nav>
  );
}
