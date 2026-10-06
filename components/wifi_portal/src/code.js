/* WiFiHub setup page (hub FW 2.1.4). Sent gzipped with its comments: keep them short. */
(function () {
  "use strict";

  var $ = function (id) { return document.getElementById(id); };
  // monotonic (the phone's clock may be set back while the page is open), and far from 0, which
  // the stamps below use for "long ago"
  var now = window.performance && performance.now ? function () { return 1e9 + performance.now(); } : Date.now;
  function show(id, on) { $(id).style.display = on ? "" : "none"; }

  // Polls (?bg=1): status 950 ms while a Connect is decided, else 3.8 s in use; list 3.8 s in use
  var IDLE_MS = 60000, POLL_MS = 3800, POLL_CONNECT_MS = 950;
  var CONNECT_TIMEOUT_MS = 30000, EMPTY_LIST_MS = 8000, RESCAN_GAP_MS = 20000;

  // status.json "reason": ESP-IDF disconnect reasons; 250: no IP
  var WRONG_PASSWORD = [2, 14, 15, 23, 202, 204];
  var NOT_FOUND = [201, 210, 211, 212];
  var NO_IP = 250;

  var views = ["view-scan", "view-password", "view-manual", "view-connecting", "view-details"];
  var currentView = "view-scan";
  var networks = [];
  // selected: {ssid, raw, chan, auth}, auth -1 typed in; connecting: {ssid, since, seenPend, timedOut}
  var selected = null;
  var connecting = null;
  var lastStatus = {};
  var lostSeen = false;
  var lastListText = null;
  var listSince = now();
  var lastActivity = now();
  var idle = false;
  var lastStatusPoll = 0, lastListPoll = 0, pointerDownAt = 0, rescanUntil = 0;
  // finished: when Finish was answered; a reply to a poll sent before epoch decides nothing;
  // resultSsid: the success view's network
  var finished = 0;
  var pollSeq = 0, lastSeq = 0, epoch = 0;
  var resultSsid = null;
  // one request of each kind at a time
  var statusBusy = false, listBusy = false, sending = false;

  function showView(id) {
    currentView = id;
    views.forEach(function (v) { $(v).style.display = v === id ? "" : "none"; });
    $(id).style.animation = "none";
    $(id).offsetHeight;
    $(id).style.animation = "";
  }

  function setStep(num) {
    document.querySelectorAll("#steps .step").forEach(function (s, i) {
      s.classList.remove("active", "done");
      if (i + 1 < num) s.classList.add("done");
      else if (i + 1 === num) s.classList.add("active");
    });
    document.querySelectorAll("#steps .step-line-fill").forEach(function (l, i) {
      l.style.width = (i + 1 < num) ? "100%" : "0";
    });
  }

  function toScan() {
    connecting = resultSsid = null;
    setStep(1);
    showView("view-scan");
    updateBanners(lastStatus);
    lastListPoll = 0;
  }

  // X-Custom-enc: pct. A "raw" SSID (not UTF-8): each code point is one of its bytes.
  function pctUtf8(s) {
    try { return encodeURIComponent(s); } catch (e) { return null; }
  }
  function pctRaw(s) {
    var out = "";
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i), ch = s.charAt(i);
      if (c > 0xFF) return null;
      out += /[A-Za-z0-9\-_.~]/.test(ch) ? ch : "%" + (c < 16 ? "0" : "") + c.toString(16).toUpperCase();
    }
    return out;
  }
  function byteLen(pct) { return pct.replace(/%[0-9A-Fa-f]{2}/g, "x").length; }

  // auth -1 (typed in): empty = open
  function passwordError(pwd, auth) {
    if (auth === 0) return null;
    if (pwd === "") return auth > 0 ? "Password is required" : null;
    var enc = pctUtf8(pwd);
    if (enc === null) return CONNECT_ERRORS.enc;
    var n = byteLen(enc);
    // WEP
    if (auth === 1) return n <= 64 ? null : "The password is too long";
    if (n === 64 && /^[0-9A-Fa-f]{64}$/.test(pwd)) return null;
    if (n < 8 || n > 63) return "Wi-Fi passwords have 8 to 63 characters (or 64 hex digits)";
    return null;
  }

  // every request, its body included, gives up after 8 s: a lost one must not hang the page
  function req(url, opts) {
    opts = opts || {};
    opts.cache = "no-store";
    var ctl = window.AbortController ? new AbortController() : null;
    if (ctl) opts.signal = ctl.signal;
    return new Promise(function (ok, fail) {
      var t = setTimeout(function () { if (ctl) ctl.abort(); fail(); }, 8000);
      fetch(url, opts).then(function (r) {
        return r.text().then(function (body) { clearTimeout(t); ok({ ok: r.ok, status: r.status, body: body }); });
      }).then(null, function () { clearTimeout(t); fail(); });
    });
  }

  function parse(r) {
    try { return JSON.parse(r.body); } catch (e) { return null; }
  }

  function getJSON(url) {
    return req(url).then(function (r) {
      if (!r.ok) throw 0;
      return parse(r);
    });
  }

  function pageHidden() { return document.visibilityState === "hidden"; }

  // Status
  // replies older than the newest one, or than a Connect or its result, are dropped
  // the next poll is timed from the reply, unless something asked for one at once (stamp 0)
  function statusDone() {
    statusBusy = false;
    if (lastStatusPoll) lastStatusPoll = now();
  }
  function pollStatus(bg) {
    var mine = ++pollSeq;
    statusBusy = true;
    lastStatusPoll = now();
    return getJSON(bg ? "status.json?bg=1" : "status.json").then(function (d) {
      statusDone();
      if (!d || typeof d !== "object" || mine <= epoch || mine < lastSeq) return;
      lastSeq = mine;
      lastStatus = d;
      if (d.urc === 3 && d.ssid) lostSeen = true;
      if (connecting) {
        connectProgress(d);
      } else if (!finished) {
        if (resultSsid && !d.pend && !(d.urc === 0 && d.ssid === resultSsid)) toScan();
        updateBanners(d);
      }
    }, statusDone);
  }

  // urc 3: the router lost; urc 1: the last Connect failed (maybe long ago: a banner, not a view)
  function updateBanners(d) {
    var lost = d.urc === 3 && d.ssid && !d.pend;
    var failed = d.urc === 1 && d.ssid && !d.pend;
    var ok = d.urc === 0 && d.ssid && !d.pend;
    show("fallback-banner", !!(lost || failed));
    if (lost) {
      $("fallback-label").textContent = "Connection lost";
      $("fallback-text").textContent = "WiFiHub lost its connection to «" + d.ssid +
        "». It keeps retrying. Choose a network to change it.";
    } else if (failed) {
      var t = failText(d.ssid, d.reason);
      $("fallback-label").textContent = t[0];
      $("fallback-text").textContent = t[1];
    }
    show("connected-banner", !!ok);
    if (ok) {
      document.querySelector("#connected-banner .banner-label").textContent =
        lostSeen ? "WiFiHub reconnected to" : "Connected to";
      $("connected-ssid").textContent = d.ssid;
      $("details-ssid-label").textContent = d.ssid;
      $("detail-ip").textContent = d.ip || "—";
      $("detail-netmask").textContent = d.netmask || "—";
      $("detail-gw").textContent = d.gw || "—";
    }
  }

  // Network list
  function rssiBars(rssi) {
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
  }

  var lockSVG = '<svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true">'
    + '<path d="M4 7h8v7H4zM5.5 7V5a2.5 2.5 0 0 1 5 0v2" fill="none" stroke="currentColor" stroke-width="1.6"/></svg>';

  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  function renderNetworks(list) {
    networks = list;
    if (!list.length) {
      if (now() - listSince >= EMPTY_LIST_MS) {
        $("network-list").innerHTML = '<div class="scan-placeholder"><span>No networks found. ' +
          'Tap Rescan, or connect to a hidden network.</span></div>';
      }
      return;
    }
    var html = "";
    list.forEach(function (ap, i) {
      var bars = rssiBars(ap.rssi);
      html += '<div class="network" role="listitem" tabindex="0" data-i="' + i + '">'
        + '<div class="signal" data-bars="' + bars + '"><i></i><i></i><i></i><i></i></div>'
        + '<div class="net-info"><div class="net-name">' + esc(ap.ssid) + '</div>'
        + '<div class="net-meta">' + (ap.auth !== 0 ? lockSVG + " Secured" : "Open") + " &middot; "
        + ["", "Weak", "Fair", "Good", "Strong"][bars] + '</div></div></div>';
    });
    $("network-list").innerHTML = html;
  }

  // Not redrawn just after a touch: a tap never lands on a moved row.
  function listDone() {
    listBusy = false;
    if (lastListPoll) lastListPoll = now();
  }
  function refreshList(bg) {
    if (listBusy) return;
    listBusy = true;
    lastListPoll = now();
    getJSON(bg ? "ap.json?bg=1" : "ap.json").then(function (data) {
      listDone();
      if (!Array.isArray(data)) return;
      var text = JSON.stringify(data);
      if ((text === lastListText && data.length) || now() - pointerDownAt < 700) return;
      lastListText = text;
      data.sort(function (a, b) { return b.rssi - a.rssi; });
      renderNetworks(data);
    }, listDone);
  }

  function holdRescan(ms) {
    rescanUntil = now() + ms;
    (function count() {
      var left = Math.ceil((rescanUntil - now()) / 1000);
      $("btn-rescan").disabled = left > 0;
      $("rescan-label").textContent = left > 0 ? "Rescan (" + left + " s)" : "Rescan";
      if (left > 0) setTimeout(count, 1000);
    })();
  }

  function rescan() {
    if (now() < rescanUntil) return;
    $("btn-rescan").disabled = true;
    req("scan.json", { method: "POST" }).then(function (r) {
      return r.ok ? parse(r) : null;
    }).then(function (j) {
      if (j && j.scan === 1) {
        listSince = now();
        setTimeout(function () { refreshList(false); }, 3000);
        setTimeout(function () { refreshList(false); }, 6500);
        holdRescan(RESCAN_GAP_MS);
      } else {
        holdRescan(j && j["in"] > 0 ? j["in"] * 1000 : 5000);
      }
    }, function () { holdRescan(5000); });
  }

  // {lost}: no answer, the status decides
  function sendConnect(sel, pwd) {
    var s = sel.raw ? pctRaw(sel.ssid) : pctUtf8(sel.ssid), p = pctUtf8(pwd);
    if (s === null) return Promise.resolve({ err: "ssid" });
    if (p === null) return Promise.resolve({ err: "pwd" });
    var h = { "X-Custom-enc": "pct", "X-Custom-ssid": s, "X-Custom-pwd": p };
    if (sel.chan > 0) h["X-Custom-chan"] = String(sel.chan);
    return req("connect.json", { method: "POST", headers: h }).then(function (r) {
      if (r.ok) return { ok: true };
      if (r.status !== 400) return { err: "busy" };
      var j = parse(r);
      return { err: (j && j.err) || "enc" };
    }, function () { return { lost: true }; });
  }

  var CONNECT_ERRORS = {
    busy: "WiFiHub is busy - try again",
    ssid: "Network name too long (32 bytes at most)",
    pwd: "Password too long",
    enc: "A character cannot be sent"
  };

  // one at a time: a second tap (an open network's row has no button to disable) sends nothing
  function performConnect(sel, pwd, button, onError) {
    if (sending) return;
    sending = true;
    if (button) button.disabled = true;
    sendConnect(sel, pwd).then(function (res) {
      sending = false;
      if (button) button.disabled = false;
      if (res.ok || res.lost) {
        selected = sel;
        startConnecting(sel.ssid, false);
      } else {
        onError(CONNECT_ERRORS[res.err] || CONNECT_ERRORS.enc);
      }
    });
  }

  function showState(state) {
    ["loading", "success", "fail", "finished"].forEach(function (s) { show("state-" + s, s === state); });
    show("btn-retry", state === "fail");
    show("btn-done", state === "fail" || state === "success");
    show("btn-finish", state === "success");
    $("finish-note").textContent = "";
    setStep(3);
    showView("view-connecting");
  }

  function startConnecting(ssid, resumed) {
    connecting = { ssid: ssid, since: now(), seenPend: resumed, timedOut: false };
    epoch = pollSeq;
    $("connecting-ssid").textContent = ssid;
    showState("loading");
    lastStatusPoll = 0;
  }

  // "pend" until decided, then "ssid" with urc 0 or 1 and "reason"
  function connectProgress(d) {
    var c = connecting;
    if (d.pend) {
      if (d.pend === c.ssid) c.seenPend = true;
    } else if (d.ssid === c.ssid && d.urc === 0) {
      showSuccess(d.ssid);
    } else if (d.ssid === c.ssid && d.urc === 1) {
      showFailure(d.ssid, d.reason);
    } else if (c.seenPend) {
      showFailure(c.ssid, -1);
    }
  }

  function connectTimeout() {
    connecting.timedOut = true;
    show("btn-retry", true);
    show("btn-done", true);
    $("finish-note").textContent = "Still connecting to «" + connecting.ssid + "»: wait, or try again.";
  }

  function showSuccess(ssid) {
    connecting = null;
    $("success-ssid").textContent = resultSsid = ssid;
    epoch = pollSeq;
    showState("success");
  }

  function failText(ssid, reason) {
    var q = "«" + ssid + "»";
    return WRONG_PASSWORD.indexOf(reason) >= 0 ?
      ["Wrong password", "The password for " + q + " was not accepted."] :
      NOT_FOUND.indexOf(reason) >= 0 ?
      ["Network not found", q + " not found - is it 2.4 GHz and in range?"] :
      reason === NO_IP ?
      ["No IP address", q + " gave WiFiHub no IP address."] :
      ["Connection failed", "WiFiHub could not connect to " + q + "."];
  }

  function showFailure(ssid, reason) {
    var t = failText(ssid, reason);
    connecting = null;
    $("fail-title").textContent = t[0];
    $("fail-text").textContent = t[1];
    if (!selected || selected.ssid !== ssid) selected = { ssid: ssid, raw: false, chan: 0, auth: -1 };
    showState("fail");
  }

  // the setup network stops ~2 s after Finish; no answer: it may have already
  function doFinish(button) {
    button.disabled = true;
    req("finish.json", { method: "POST" }).then(function (r) {
      button.disabled = false;
      if (r.ok) return finishedView();
      if (currentView !== "view-connecting") return toScan();
      $("finish-note").textContent = r.status === 409 ? "WiFiHub is not connected yet." : "Please try again.";
    }, function () {
      button.disabled = false;
      finishedView();
    });
  }

  function finishedView() {
    finished = now();
    connecting = null;
    showState("finished");
    show("btn-done", false);
  }

  // Disconnect: the hub forgets its Wi-Fi
  function performDisconnect() {
    var busy = function () { $("dc-note").textContent = "WiFiHub is busy - try again."; };
    req("connect.json", { method: "DELETE" }).then(function (r) {
      if (!r.ok) return busy();
      selected = null;
      lastStatus = {};
      toScan();
      lastStatusPoll = 0;
    }, busy);
  }

  function noteActivity() {
    if (pageHidden()) return;
    lastActivity = now();
    if (idle) {
      idle = false;
      $("idle-hint").style.display = "none";
      lastStatusPoll = lastListPoll = 0;
    }
  }

  function tick() {
    var t = now();
    if (pageHidden()) return;
    if (finished) {
      // 15 s on, the setup network still answers (its stop did not come): back to the page
      if (t - finished >= 15000 && t - lastStatusPoll >= POLL_MS) {
        lastStatusPoll = t;
        getJSON("status.json?bg=1").then(function () { finished = 0; toScan(); }, function () {});
      }
      return;
    }
    if (!idle && t - lastActivity >= IDLE_MS) {
      idle = true;
      $("idle-hint").style.display = "";
    }
    if (connecting) {
      if (!connecting.timedOut && t - connecting.since >= CONNECT_TIMEOUT_MS) connectTimeout();
      if (!statusBusy && t - lastStatusPoll >= (connecting.timedOut ? POLL_MS : POLL_CONNECT_MS)) pollStatus(true);
      return;
    }
    if (idle) return;
    if (!statusBusy && t - lastStatusPoll >= POLL_MS) pollStatus(true);
    if (t - lastListPoll >= POLL_MS) refreshList(true);
  }

  function clearErr(input, errEl) {
    $(errEl).textContent = "";
    $(input).classList.remove("field-err");
  }

  function openPassword(sel) {
    selected = sel;
    $("pwd-net-name").textContent = sel.ssid;
    $("input-pwd").value = "";
    clearErr("input-pwd", "pwd-error");
    setStep(2);
    showView("view-password");
    setTimeout(function () { $("input-pwd").focus(); }, 100);
  }

  function openManual(ssid) {
    $("input-manual-ssid").value = ssid;
    $("input-manual-pwd").value = "";
    clearErr("input-manual-ssid", "ssid-error");
    setStep(2);
    showView("view-manual");
    setTimeout(function () { $(ssid ? "input-manual-pwd" : "input-manual-ssid").focus(); }, 100);
  }

  function chooseNetwork(ap) {
    var sel = { ssid: ap.ssid, raw: ap.raw === 1, chan: ap.chan | 0, auth: ap.auth | 0 };
    if (sel.auth !== 0) return openPassword(sel);
    // an open network: no password
    performConnect(sel, "", null, function (msg) {
      selected = sel;
      $("fail-title").textContent = "Connection failed";
      $("fail-text").textContent = msg;
      showState("fail");
    });
  }

  function retry() {
    var sel = selected;
    connecting = null;
    if (!sel) return toScan();
    for (var i = 0; i < networks.length; i++) {
      if (networks[i].ssid === sel.ssid) return chooseNetwork(networks[i]);
    }
    if (sel.auth >= 0) openPassword(sel);
    else openManual(sel.ssid);
  }

  function fieldError(input, errEl, msg) {
    $(errEl).textContent = msg;
    $(input).classList.add("field-err");
    $(input).focus();
  }

  function on(id, type, fn) { $(id).addEventListener(type, fn); }

  function init() {
    document.querySelectorAll("[data-toggle-pwd]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        $(btn.getAttribute("data-toggle-pwd")).type = btn.classList.toggle("revealed") ? "text" : "password";
      });
    });
    document.querySelectorAll("[data-back]").forEach(function (btn) {
      btn.addEventListener("click", function () { selected = null; toScan(); });
    });

    ["pointerdown", "touchstart", "mousedown", "keydown", "input", "wheel", "scroll", "focusin"]
      .forEach(function (type, i) {
        document.addEventListener(type, function () {
          if (i < 3) pointerDownAt = now();
          noteActivity();
        }, { capture: true, passive: true });
      });
    window.addEventListener("focus", noteActivity);
    window.addEventListener("pageshow", noteActivity);
    document.addEventListener("visibilitychange", function () { idle = true; noteActivity(); });

    on("network-list", "click", function (e) {
      var row = e.target.closest(".network");
      var ap = row && networks[parseInt(row.getAttribute("data-i"), 10)];
      if (ap) chooseNetwork(ap);
    });
    on("network-list", "keydown", function (e) {
      var row = e.target.closest(".network");
      if (row && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); row.click(); }
    });
    on("btn-hidden", "click", function () { openManual(""); });
    on("btn-rescan", "click", rescan);

    on("btn-connect", "click", function () {
      var pwd = $("input-pwd").value;
      var err = passwordError(pwd, selected ? selected.auth : 3);
      if (err) return fieldError("input-pwd", "pwd-error", err);
      clearErr("input-pwd", "pwd-error");
      performConnect(selected, pwd, $("btn-connect"), function (msg) { $("pwd-error").textContent = msg; });
    });
    on("input-pwd", "input", function () { clearErr("input-pwd", "pwd-error"); });
    on("input-pwd", "keydown", function (e) { if (e.key === "Enter") $("btn-connect").click(); });

    // sent as typed: spaces at either end belong to an SSID
    on("btn-manual-connect", "click", function () {
      var ssid = $("input-manual-ssid").value, pwd = $("input-manual-pwd").value;
      var enc = pctUtf8(ssid);
      var err = ssid === "" ? "Network name is required" :
        enc === null ? CONNECT_ERRORS.enc :
        byteLen(enc) > 32 ? "The network name is too long (32 bytes at most)" : null;
      if (err) return fieldError("input-manual-ssid", "ssid-error", err);
      err = passwordError(pwd, -1);
      if (err) return fieldError("input-manual-pwd", "ssid-error", err);
      clearErr("input-manual-ssid", "ssid-error");
      performConnect({ ssid: ssid, raw: false, chan: 0, auth: -1 }, pwd, $("btn-manual-connect"),
        function (msg) { $("ssid-error").textContent = msg; });
    });
    on("input-manual-ssid", "input", function () { clearErr("input-manual-ssid", "ssid-error"); });
    on("input-manual-ssid", "keydown", function (e) { if (e.key === "Enter") $("input-manual-pwd").focus(); });
    on("input-manual-pwd", "keydown", function (e) { if (e.key === "Enter") $("btn-manual-connect").click(); });

    on("btn-done", "click", function () { selected = null; toScan(); });
    on("btn-retry", "click", retry);
    on("btn-finish", "click", function () { doFinish($("btn-finish")); });
    on("btn-finish-details", "click", function () { doFinish($("btn-finish-details")); });
    on("connected-banner", "click", function () { $("dc-note").textContent = ""; showView("view-details"); });

    on("btn-disconnect", "click", function () { show("modal-disconnect", true); });
    on("btn-cancel-dc", "click", function () { show("modal-disconnect", false); });
    on("btn-confirm-dc", "click", function () { show("modal-disconnect", false); performDisconnect(); });

    // the status first, then the view and the list
    pollStatus(false).then(function () {
      var d = lastStatus;
      if (d.pend) startConnecting(d.pend, true);
      else if (d.ssid && d.urc === 0) showSuccess(d.ssid);
      else updateBanners(d);
      listSince = now();
      refreshList(false);
      setInterval(tick, 250);
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
