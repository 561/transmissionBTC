(function () {
  var base = location.protocol + '//' + location.hostname + ':9093/idle/';
  var box = document.createElement('span');
  box.style.cssText = 'color:#fff;font:13px sans-serif;padding:3px 8px;border-radius:4px;margin-right:12px;' +
    'display:inline-flex;gap:6px;align-items:center;vertical-align:middle;white-space:nowrap';
  var label = document.createElement('span');
  box.appendChild(label);

  function button(text, path) {
    var b = document.createElement('button');
    b.textContent = text;
    b.style.cssText = 'cursor:pointer;border:0;border-radius:3px;padding:2px 7px;background:rgba(255,255,255,.25);color:#fff;font:12px sans-serif';
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
  function placeBox() {
    var theme = document.getElementById('select-themes');
    var themeLabel = theme && theme.previousElementSibling;

    if (themeLabel && themeLabel.parentNode) {
      themeLabel.parentNode.insertBefore(box, themeLabel);
    } else {
      box.style.cssText += ';position:fixed;right:12px;bottom:12px;z-index:99999;padding:8px 10px';
      document.body.appendChild(box);
    }
  }

  placeBox();
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

  var deleteUrl = location.protocol + '//' + location.hostname + ':9093/file/delete?path=';

  function deleteSelectedFiles() {
    var rows = $('#torrent-files-table').datagrid('getChecked');
    var id = system.currentTorrentId;

    if (!rows.length || !id) {
      alert('Отметь галочками файлы, которые нужно удалить');
      return;
    }

    var indexes = rows.map(function (row) { return parseInt(row.index, 10); });

    if (!confirm('Удалить файлов: ' + indexes.length + ' — с флешки, и больше их не качать?')) return;

    transmission.exec({ method: 'torrent-set', arguments: { ids: [id], 'files-unwanted': indexes } }, function () {
      transmission.exec({ method: 'torrent-get', arguments: { ids: [id], fields: ['files', 'downloadDir'] } }, function (res) {
        var torrent = res.arguments.torrents[0];
        var dir = torrent.downloadDir.replace(/\/$/, '');
        var requests = indexes.map(function (i) {
          return fetch(deleteUrl + encodeURIComponent(dir + '/' + torrent.files[i].name), { method: 'POST' })
            .then(function (r) { return r.json(); })
            .catch(function () { return { deleted: false }; });
        });

        Promise.all(requests).then(function (results) {
          var removed = results.filter(function (r) { return r.deleted; }).length;
          alert('Больше не качается: ' + indexes.length + ', удалено с флешки: ' + removed);
          system.reloadData();
        });
      });
    });
  }

  function addDeleteButton() {
    if (typeof $ === 'undefined' || !$('#torrent-files-table').length) return false;
    var table = $('#torrent-files-table');
    var buttons = table.data('buttons');
    if (!buttons) return false;

    if (!buttons.some(function (b) { return b.id === 'pager-button-delete'; })) {
      var at = buttons.findIndex(function (b) { return b.id === 'torrent-files-filter'; });
      var button = {
        id: 'pager-button-delete',
        iconCls: 'icon-remove-torrent',
        title: 'Удалить отмеченные файлы с флешки и не качать',
        handler: deleteSelectedFiles
      };
      buttons.splice(at < 0 ? buttons.length : at, 0, button, '-');
      table.datagrid('getPager').pagination({ buttons: buttons });
    }

    return true;
  }

  var buttonTimer = setInterval(function () {
    try {
      if (addDeleteButton()) clearInterval(buttonTimer);
    } catch (ignore) {}
  }, 1000);
})();
