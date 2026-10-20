"""Show startup preparation messages from the private game log."""
import threading


class LaunchProgress:
    def __init__(self, path):
        self.path = path
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.thread.start()

    def finish(self):
        self.stop.set()
        self.thread.join(timeout=2)

    def run(self):
        while not self.stop.wait(0.25):
            try:
                source = self.path.open(errors="replace")
                break
            except FileNotFoundError:
                pass
        else:
            return
        with source:
            while not self.stop.is_set():
                line = source.readline()
                if not line:
                    self.stop.wait(0.25)
                    continue
                for marker in ("Metal preparation:", "Metal: pipeline prewarm:", "Metal pipeline disk cache:"):
                    if marker in line:
                        print(line[line.index(marker):].strip(), flush=True)
                        break
                if "frame stats:" in line:
                    print("FM4 is producing game frames. F3 toggles the FPS display.", flush=True)
                    return
