// 番茄钟+：SpareScript 示例。界面、计时、动画全部在眼镜本地运行，不需要手机。
// 手机控制面板（app.json controls）：时长、休息、进度条开关、样式、标题、清零；累计完成数用 ctl_set 显示到手机上。
persist var minutes = 25;      // 上次设的时长（重启后还在）
persist var finished = 0;      // 累计完成数
var left = 0;                  // 剩余秒数
var running = 0;
var resting = 0;

fn mmss(sec) { return pad(sec / 60, 2) + ":" + pad(sec % 60, 2); }

fn style() {
  var s = ctl("style");                  // picker：选项下标
  if (s == 2) { return 120; }
  if (s == 1) { return 200; }
  return 255;
}

fn render() {
  text(title, ctl("title"));
  text(time, mmss(left));
  show(bar, ctl("bar"));
  if (resting) { value(bar, left * 100 / (ctl("rest") * 60)); }
  else { value(bar, 100 - left * 100 / (minutes * 60)); }
  if (finished > 0) { text(count, "已完成 " + finished); } else { text(count, ""); }
  if (resting) { text(hint, "休息一下 · 单击跳过"); gray(time, 140); }
  else if (running) { text(hint, "单击暂停"); gray(time, style()); }
  else if (left == minutes * 60) { text(hint, "单击开始 · 转动表冠调时长"); gray(time, style()); }
  else { text(hint, "已暂停 · 单击继续"); gray(time, 140); }
}

fn reset_time() { resting = 0; left = minutes * 60; }

on open() { minutes = ctl("minutes"); reset_time(); render(); }

// 用户在手机控制面板上动了一个控件（id 是控件的 id）；id == "*"：应用刚打开，手机补发了全部值
on control(id) {
  if ((id == "minutes" || id == "*") && !running) { minutes = ctl("minutes"); reset_time(); }
  if (id == "reset") { finished = 0; save(); ctl_set("finished", 0); }
  render();
}

on crown(d) {
  if (running || resting) { return; }
  minutes = clamp(minutes + d * 5, 5, 60);
  timer(2, 800, 0);                      // 停手 0.8 秒后再告诉手机（请求限频 1 次/秒）
  reset_time();
  render();
  anim_scale(time, 236, 256, 240);
}

on click() {
  if (resting) { timer(0, 0, 0); running = 0; reset_time(); render(); return; }
  running = !running;
  if (running) { timer(0, 1000, 1); } else { timer(0, 0, 0); }
  render();
  anim_scale(time, 280, 256, 300);
}

on timer(id) {
  if (id == 1) { show(done, 0); render(); return; }
  if (id == 2) { ctl_set("minutes", minutes); return; }   // 手机上的滑块跟着变
  left -= 1;
  if (left <= 0 && resting) { running = 0; timer(0, 0, 0); reset_time(); }
  else if (left <= 0) {
    finished += 1;
    save();
    ctl_set("finished", finished);
    resting = 1;
    left = ctl("rest") * 60;
    show(done, 1);
    anim_opa(done, 0, 255, 300);
    timer(1, 2500, 0);
  }
  render();
}
