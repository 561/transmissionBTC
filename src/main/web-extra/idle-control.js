(function () {
  var base = location.protocol + '//' + location.hostname + ':9093/idle/';
  var box = document.createElement('div');
  box.style.cssText = 'position:fixed;right:12px;bottom:12px;z-index:99999;background:#222;color:#fff;' +
    'font:13px sans-serif;padding:8px 10px;border-radius:6px;box-shadow:0 2px 8px rgba(0,0,0,.4);' +
    'display:flex;gap:6px;align-items:center;flex-wrap:wrap;max-width:420px';
  var label = document.createElement('span');
  box.appendChild(label);

  function button(text, path) {
    var b = document.createElement('button');
    b.textContent = text;
    b.style.cssText = 'cursor:pointer;border:0;border-radius:4px;padding:4px 8px;background:#555;color:#fff';
    b.onclick = function () { call('POST', path); };
    box.appendChild(b);
    return b;
  }

  var runBtn = button('Качать сейчас', 'run');
  var pauseBtn = button('Пауза', 'pause');
  var autoBtn = button('Авто', 'auto');

  function describe(s) {
    if (!s.running) return '⏹ Движок остановлен';
    if (s.suspended) {
      if (s.reason === 'busy') return '⏸ Пауза: приставка используется';
      if (s.reason === 'forced') return '⏸ Пауза вручную';
      if (s.reason === 'user') return '⏸ Остановлено в приложении';
      return '⏸ Пауза: нет сети';
    }
    if (s.override === 'run') return '⬇ Качает принудительно (до следующего включения экрана)';
    return s.idle ? '⬇ Качает: приставка простаивает' : '⬇ Качает';
  }

  function render(s) {
    label.textContent = describe(s);
    box.style.background = s.suspended ? '#8a4b00' : '#1b6b2f';
    runBtn.style.display = s.suspended && s.reason !== 'user' ? '' : 'none';
    pauseBtn.style.display = !s.suspended || s.reason === 'busy' ? '' : 'none';
    autoBtn.style.display = s.override !== 'auto' ? '' : 'none';
  }

  function call(method, path) {
    fetch(base + path, { method: method })
      .then(function (r) { return r.json(); })
      .then(render)
      .catch(function () {
        label.textContent = 'нет связи с приставкой';
        box.style.background = '#444';
      });
  }

  function poll() { call('GET', 'status'); }
  document.body.appendChild(box);
  poll();
  setInterval(poll, 3000);

  var lastReload = Date.now();

  function watchReloadLoop() {
    if (typeof system === 'undefined' || !system.config || typeof system.reloadData !== 'function') return;

    if (!system.reloadWrapped) {
      var original = system.reloadData;
      system.reloadData = function () {
        lastReload = Date.now();
        return original.apply(system, arguments);
      };
      system.reloadWrapped = true;
    }

    if (system.config.autoReload && Date.now() - lastReload > 12000) {
      system.reloading = false;
      system.reloadData();
    }
  }

  setInterval(watchReloadLoop, 5000);
})();
