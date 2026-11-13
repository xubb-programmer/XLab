export class StatsPanel {
  constructor() {
    this.elUavStatus = document.getElementById("stat-platform-count");
    this.elFirepower = document.getElementById("stat-firepower-count");
    this.elTargetCount = document.getElementById("stat-target-count");
    this.elSource = document.getElementById("stat-data-source");
    this.elClock = document.getElementById("military-clock");
    this.initClock();
  }

  initClock() {
    if (!this.elClock) return;
    const clock = new Intl.DateTimeFormat("zh-CN", {
      timeZone: "Asia/Shanghai",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hourCycle: "h23"
    });
    const update = () => {
      this.elClock.textContent = clock.format(new Date());
    };
    update();
    setInterval(update, 1000);
  }

  update({ uavs = [], points = [], targets = [], source = "" }) {
    const burstCount = targets.filter(target => target.type === "BURST_POINT").length;
    const reconCount = targets.length - burstCount;

    if (this.elUavStatus) {
      this.elUavStatus.textContent = `${uavs.length} 台`;
    }
    if (this.elFirepower) {
      this.elFirepower.textContent = `${points.length} 处`;
    }
    if (this.elTargetCount) {
      this.elTargetCount.textContent = `${reconCount} / ${burstCount}`;
      this.elTargetCount.classList.toggle("threat", targets.length > 0);
    }
    if (this.elSource) {
      const [dot, label] = this.elSource.children;
      const isConnected = source === "已订阅";
      dot.classList.toggle("online", isConnected);
      dot.classList.toggle("standby", !isConnected);
      label.textContent = source;
    }
  }
}
