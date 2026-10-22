(function () {
  var base = location.protocol + '//' + location.hostname + ':9093/idle/';
  var box = document.createElement('span');
  box.style.cssText = 'font:inherit;color:inherit;margin-right:16px;display:inline-flex;gap:8px;' +
    'align-items:center;vertical-align:middle;white-space:nowrap';
  var dot = document.createElement('span');
  dot.style.cssText = 'width:8px;height:8px;border-radius:50%;display:inline-block;background:#999';
  var label = document.createElement('span');
  box.appendChild(dot);
  box.appendChild(label);

  function button(text, path) {
    var b = document.createElement('a');
    b.href = 'javascript:;';
    b.textContent = text;
    b.className = 'l-btn l-btn-small';
    b.style.cssText = 'font:inherit;color:inherit;border:1px solid #bbb;border-radius:5px;padding:2px 8px;' +
      'text-decoration:none;cursor:pointer;background:transparent;line-height:16px';
    b.onmouseenter = function () { b.style.background = 'rgba(0,0,0,.08)'; };
    b.onmouseleave = function () { b.style.background = 'transparent'; };
    b.onclick = function () { call('POST', path); return false; };
    box.appendChild(b);
    return b;
  }

  var runBtn = button('Download now', 'run');
  var pauseBtn = button('Pause', 'pause');
  var autoBtn = button('Auto', 'auto');

  function describe(s) {
    if (!s.running) return 'Engine stopped';
    if (s.suspended) {
      if (s.reason === 'busy') return 'Paused: TV in use';
      if (s.reason === 'forced') return 'Paused manually';
      if (s.reason === 'user') return 'Stopped in the app';
      return 'Paused: no network';
    }
    if (s.override === 'run') return 'Downloading: forced until the screen turns on';
    return s.idle ? 'Downloading: TV idle' : 'Downloading';
  }

  function render(s) {
    label.textContent = describe(s);
    dot.style.background = s.suspended ? '#e08a00' : '#2e9e47';
    runBtn.style.display = s.suspended && s.reason !== 'user' ? '' : 'none';
    pauseBtn.style.display = !s.suspended || s.reason === 'busy' ? '' : 'none';
    autoBtn.style.display = s.override !== 'auto' ? '' : 'none';
  }

  function call(method, path) {
    fetch(base + path, { method: method })
      .then(function (r) { return r.json(); })
      .then(render)
      .catch(function () {
        label.textContent = 'TV not reachable';
        dot.style.background = '#999';
      });
  }

  function poll() { call('GET', 'status'); }
  function placeBox() {
    var theme = document.getElementById('select-themes');
    var themeLabel = theme && theme.previousElementSibling;

    if (themeLabel && themeLabel.parentNode) {
      themeLabel.parentNode.insertBefore(box, themeLabel);
    } else {
      box.style.cssText += ';position:fixed;right:12px;bottom:12px;z-index:99999;padding:6px 10px;background:#fff;color:#444;border:1px solid #bbb;border-radius:5px';
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
      alert('Check the files you want to delete');
      return;
    }

    var indexes = rows.map(function (row) { return parseInt(row.index, 10); });

    if (!confirm('Delete ' + indexes.length + ' file(s) from the drive and stop downloading them?')) return;

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
          alert('Skipped: ' + indexes.length + ', deleted from the drive: ' + removed);
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
        title: 'Delete checked files from the drive and skip them',
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
