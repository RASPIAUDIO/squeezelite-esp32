/* Sendspin controls share the existing System tab and Bootstrap theme. */
(() => {
  function mount() {
    const tab = document.getElementById('tab-cfg-syst');
    const template = document.getElementById('sendspin-system-template');
    if (!tab || !template || document.getElementById('sendspin-system-card')) return;
    tab.appendChild(template.content.cloneNode(true));
    const card = document.getElementById('sendspin-system-card');
    const el = id => document.getElementById('ss-' + id);
    const text = (id, value) => { el(id).textContent = value; };
    let loaded = false, busy = false, polling = false, dirty = false;
    let current = null, hideTimer, restartUntil = 0;
    // The legacy app builds Spotify cards asynchronously from commands.json.
    function position() {
      const anchor = document.getElementById('spotify-config-card') ||
        document.getElementById('flds-cfg-syst-cspot')?.closest('.card');
      const airplay = document.getElementById('cfg-syst-services-AirPlay')?.closest('.form-check');
      const after = airplay?.nextElementSibling?.tagName === 'SMALL' ? airplay.nextElementSibling : airplay;
      if (after && after.nextElementSibling !== el('service')) after.after(el('service'));
      if (anchor && anchor.parentNode === tab && anchor.nextElementSibling !== card) anchor.after(card);
    }
    new MutationObserver(position).observe(tab, {childList: true});
    function visible() { return !document.hidden && tab.classList.contains('active'); }
    function hideToken() {
      clearTimeout(hideTimer); el('token').value = '';
      el('token-area').hidden = el('hide').hidden = true;
    }
    function controls() {
      ['enabled', 'name', 'save', 'apply', 'forget'].forEach(id => { el(id).disabled = busy || !loaded; });
      el('reveal').disabled = busy || !loaded || !current?.running;
      el('hide').disabled = busy;
    }
    function options() {
      const enabled = el('enabled').checked;
      el('options').hidden = el('pairing').hidden = !enabled;
      el('enabled').setAttribute('aria-expanded', String(enabled));
      if (!enabled) hideToken();
    }
    async function api(data, path = '/sendspin.json') {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 8000);
      try {
        const r = await fetch(path, {
          method: data ? 'POST' : 'GET', cache: 'no-store', signal: controller.signal,
          headers: data ? {'Content-Type': 'application/json', 'X-MnCast-Action': 'sendspin'} : {},
          body: data ? JSON.stringify(data) : undefined
        });
        if (path === '/reboot.json') {
          if (!r.ok) throw Error('Settings saved, but restart failed. Click Apply to retry.');
          return;
        }
        const result = await r.json();
        if (!r.ok) throw Error(result.error || 'Request failed');
        return result;
      } finally { clearTimeout(timeout); }
    }
    async function refresh() {
      if (busy || polling || !visible() || Date.now() < restartUntil) return;
      polling = true;
      try {
        const s = await api();
        if (busy) return;
        current = s;
        if (!s.supported) {
          text('state', 'Sendspin is not available in this firmware'); loaded = false; controls(); return;
        }
        text('state', s.identity_error ? 'Pairing storage error' : s.playing ? 'Playing' :
          s.connected ? 'Connected — idle' : s.running ? 'Waiting for a Sendspin server' :
          s.enabled ? 'Stopped' : 'Disabled');
        text('server', s.connected ? (s.server || 'Connected server') : '—');
        text('stream', s.playing ? `${s.codec} · ${s.bit_depth}-bit · ${s.sample_rate / 1000} kHz · ${s.channels} channels` : '—');
        text('title', s.title || '—'); text('sync', s.connected ? (s.time_synced ? 'Yes' : 'Synchronizing') : '—');
        text('pairings', s.pairings ?? '—');
        if (!loaded || !dirty) {
          el('name').value = s.name; el('enabled').checked = s.enabled; options();
        }
        loaded = true; controls();
        if (!dirty) text('message', s.restart_required ? 'Settings saved. Click Apply to restart and use them.' : '');
      } catch (e) { text('state', 'Player unreachable — retrying'); }
      finally { polling = false; }
    }
    async function save(restart, forget = false) {
      if (!loaded || busy) return;
      const name = el('name').value.trim();
      if (!name || new TextEncoder().encode(name).length > 63 || /[\x00-\x1f\x7f]/.test(name)) {
        text('message', 'Enter a player name of 1–63 UTF-8 bytes without control characters.'); return;
      }
      if (forget && !confirm('Forget all Sendspin pairings and restart? You will need to pair Music Assistant again.')) return;
      busy = true; controls(); hideToken();
      try {
        await api({action: forget ? 'forget' : 'save', enabled: el('enabled').checked, name,
          confirm: forget ? 'forget-pairings' : undefined});
        dirty = false;
        if (restart) {
          text('message', 'Settings saved. Restarting the player...');
          await api({}, '/reboot.json'); loaded = false; restartUntil = Date.now() + 12000;
          setTimeout(() => { busy = false; controls(); text('message', 'Reconnecting...'); refresh(); }, 12000);
        } else {
          text('message', 'Settings saved. Click Apply to restart and use them.'); busy = false; controls();
        }
      } catch (e) {
        text('message', e.name === 'AbortError' ? 'Request timed out. Check the player status before retrying.' : e.message);
        busy = false; controls();
      }
    }
    // Persist Sendspin before the existing Services handler can schedule a reboot.
    // Sentinel option attributes make the legacy command builder ignore this input.
    let forwardingServices = false;
    tab.addEventListener('click', async e => {
      const button = e.target.closest('#btn-save-cfg-syst-services, #btn-commit-cfg-syst-services');
      if (!button || forwardingServices) return;
      e.preventDefault(); e.stopImmediatePropagation();
      if (!loaded || busy) { text('service-message', 'Please wait for Sendspin settings to load.'); return; }
      busy = true; controls();
      try {
        await api({action: 'save', enabled: el('enabled').checked, name: current.name});
        text('service-message', 'Sendspin setting saved. Apply restarts the player.');
        forwardingServices = true;
        try { button.click(); } finally { forwardingServices = false; }
      } catch (error) {
        text('service-message', 'Could not save Sendspin. Services were not applied. Please retry.');
      } finally { busy = false; controls(); }
    }, true);
    el('form').addEventListener('submit', e => { e.preventDefault(); save(false); });
    el('apply').addEventListener('click', () => save(true));
    el('forget').addEventListener('click', () => save(true, true));
    el('name').addEventListener('input', () => { dirty = true; });
    el('enabled').addEventListener('change', () => { dirty = true; options(); });
    el('hide').addEventListener('click', hideToken);
    el('reveal').addEventListener('click', async () => {
      busy = true; controls();
      try {
        const result = await api({action: 'token'});
        if (visible() && el('enabled').checked) {
          el('token').value = result.token; el('token-area').hidden = el('hide').hidden = false;
          clearTimeout(hideTimer); hideTimer = setTimeout(hideToken, 60000);
          el('token').focus(); el('token').select();
        }
      } catch (e) { text('message', e.name === 'AbortError' ? 'Request timed out.' : e.message); }
      finally { busy = false; controls(); }
    });
    new MutationObserver(() => { if (visible()) refresh(); else hideToken(); })
      .observe(tab, {attributes: true, attributeFilter: ['class']});
    document.addEventListener('visibilitychange', () => { if (visible()) refresh(); else hideToken(); });
    window.addEventListener('pagehide', hideToken);
    function openLinkedTab() {
      if (location.hash === '#tab-cfg-syst') document.querySelector('a[href="#tab-cfg-syst"]')?.click();
    }
    window.addEventListener('hashchange', openLinkedTab);
    if (document.readyState === 'complete') openLinkedTab();
    else window.addEventListener('load', openLinkedTab, {once: true});
    position(); refresh(); setInterval(refresh, 3000);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount, {once: true});
  else mount();
})();
