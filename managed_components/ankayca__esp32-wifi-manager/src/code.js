/* ── WiFiHub Captive Portal ─────────────────────────────
 * Vanilla JS – no dependencies
 * Endpoints: GET ap.json, POST/DELETE connect.json, GET status.json
 * ────────────────────────────────────────────────────── */

(function () {
  "use strict";

  // ── Helpers ──────────────────────────────────────────
  var $ = function (id) { return document.getElementById(id); };

  var selectedSSID = "";
  var refreshTimer = null;
  var statusTimer = null;

  // ── View Management ─────────────────────────────────
  var views = ["view-scan", "view-password", "view-manual", "view-connecting", "view-details"];

  function showView(id) {
    views.forEach(function (v) {
      $(v).style.display = v === id ? "" : "none";
    });
    // Re-trigger entry animation
    $(id).style.animation = "none";
    $(id).offsetHeight; // force reflow
    $(id).style.animation = "";
  }

  // ── Step Indicator ──────────────────────────────────
  function setStep(num) {
    var steps = document.querySelectorAll("#steps .step");
    var lines = document.querySelectorAll("#steps .step-line-fill");
    steps.forEach(function (s, i) {
      var idx = i + 1;
      s.classList.remove("active", "done");
      if (idx < num) s.classList.add("done");
      else if (idx === num) s.classList.add("active");
    });
    lines.forEach(function (l, i) {
      l.style.width = (i + 1 < num) ? "100%" : "0";
    });
  }

  // ── Timers ──────────────────────────────────────────
  function stopStatus() {
    if (statusTimer) { clearInterval(statusTimer); statusTimer = null; }
  }
  function stopRefresh() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
  }
  function startStatus() {
    stopStatus();
    statusTimer = setInterval(checkStatus, 950);
  }
  function startRefresh() {
    stopRefresh();
    refreshTimer = setInterval(refreshAP, 3800);
  }

  // ── Signal Helpers ──────────────────────────────────
  function rssiBars(rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    return 1;
  }
  function rssiLabel(bars) {
    return ["", "Weak", "Fair", "Good", "Strong"][bars] || "";
  }

  // Lock icon SVG (inline, tiny)
  var lockSVG = '<svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true">'
    + '<path d="M8 1a3 3 0 0 0-3 3v2H4.5A1.5 1.5 0 0 0 3 7.5v5A1.5 1.5 0 0 0 4.5 14h7a1.5 1.5 0 0 0 1.5-1.5v-5A1.5 1.5 0 0 0 11.5 6H11V4a3 3 0 0 0-3-3zm-1.5 3a1.5 1.5 0 1 1 3 0v2h-3V4z" fill="currentColor"/>'
    + '</svg>';

  // ── Build Network List HTML ─────────────────────────
  function renderNetworks(list) {
    if (!list || list.length === 0) return;

    var html = "";
    list.forEach(function (ap) {
      var bars = rssiBars(ap.rssi);
      var secured = ap.auth !== 0;
      var label = rssiLabel(bars);
      var meta = (secured ? lockSVG + " Secured" : "Open")
        + " &middot; " + label;

      html += '<div class="network" role="listitem" tabindex="0"'
        + ' data-ssid="' + escAttr(ap.ssid) + '"'
        + ' data-auth="' + ap.auth + '">'
        + '<div class="signal" data-bars="' + bars + '"><i></i><i></i><i></i><i></i></div>'
        + '<div class="net-info">'
        + '<div class="net-name">' + esc(ap.ssid) + '</div>'
        + '<div class="net-meta">' + meta + '</div>'
        + '</div></div>';
    });

    $("network-list").innerHTML = html;
  }

  // ── Escape Helpers ──────────────────────────────────
  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }
  function escAttr(s) {
    return s.replace(/&/g, "&amp;").replace(/"/g, "&quot;")
      .replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  // ── Fetch AP List ───────────────────────────────────
  async function refreshAP() {
    try {
      var res = await fetch("ap.json");
      var data = await res.json();
      if (data.length > 0) {
        data.sort(function (a, b) { return b.rssi - a.rssi; });
        renderNetworks(data);
      }
    } catch (e) {
      console.info("ap.json fetch failed");
    }
  }

  // ── Check Connection Status ─────────────────────────
  async function checkStatus() {
    try {
      var res = await fetch("status.json");
      var d = await res.json();
      if (!d || !d.ssid) {
        // No SSID — check for manual disconnect
        if (d && d.urc === 2) {
          $("connected-banner").style.display = "none";
        }
        return;
      }

      if (d.ssid === selectedSSID) {
        // We initiated this connection — update result screen
        switch (d.urc) {
          case 0: // connected
            showConnectResult(true, d);
            break;
          case 1: // failed
            showConnectResult(false, d);
            break;
        }
      } else if (d.urc === 0 && d.ssid) {
        // Already connected (not user-initiated this session)
        updateConnectedState(d);
      }
    } catch (e) {
      console.info("status.json fetch failed");
    }
  }

  function showConnectResult(success, d) {
    $("state-loading").style.display = "none";
    $("btn-done").disabled = false;

    if (success) {
      $("state-success").style.display = "";
      $("state-fail").style.display = "none";
      $("btn-retry").style.display = "none";
      updateConnectedState(d);
      setStep(3);
    } else {
      $("state-fail").style.display = "";
      $("state-success").style.display = "none";
      $("btn-retry").style.display = "";
      $("connected-banner").style.display = "none";
    }
  }

  function updateConnectedState(d) {
    $("connected-ssid").textContent = d.ssid;
    $("connected-banner").style.display = "";
    $("details-ssid-label").textContent = d.ssid;
    $("detail-ip").textContent = d.ip || "—";
    $("detail-netmask").textContent = d.netmask || "—";
    $("detail-gw").textContent = d.gw || "—";
  }

  // ── Perform Connect ─────────────────────────────────
  async function performConnect(ssid, pwd) {
    selectedSSID = ssid;
    stopStatus();
    stopRefresh();

    // Reset connecting view
    $("state-loading").style.display = "";
    $("state-success").style.display = "none";
    $("state-fail").style.display = "none";
    $("btn-done").disabled = true;
    $("btn-retry").style.display = "none";
    $("connecting-ssid").textContent = ssid;

    setStep(3);
    showView("view-connecting");

    try {
      await fetch("connect.json", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-Custom-ssid": ssid,
          "X-Custom-pwd": pwd
        },
        body: JSON.stringify({ timestamp: Date.now() })
      });
    } catch (e) {
      console.info("connect.json POST failed");
    }

    startStatus();
    startRefresh();
  }

  // ── Disconnect ──────────────────────────────────────
  async function performDisconnect() {
    stopStatus();
    selectedSSID = "";

    try {
      await fetch("connect.json", {
        method: "DELETE",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ timestamp: Date.now() })
      });
    } catch (e) {
      console.info("connect.json DELETE failed");
    }

    $("connected-banner").style.display = "none";
    startStatus();
    setStep(1);
    showView("view-scan");
  }

  // ── Password Show/Hide Toggle ───────────────────────
  function initEyeToggles() {
    document.querySelectorAll("[data-toggle-pwd]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var inputId = btn.getAttribute("data-toggle-pwd");
        var input = $(inputId);
        var revealed = btn.classList.toggle("revealed");
        input.type = revealed ? "text" : "password";
      });
    });
  }

  // ── Back Buttons ────────────────────────────────────
  function initBackButtons() {
    document.querySelectorAll("[data-back]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        selectedSSID = "";
        setStep(1);
        showView("view-" + btn.getAttribute("data-back"));
      });
    });
  }

  // ── Initialization ──────────────────────────────────
  function init() {
    initEyeToggles();
    initBackButtons();

    // ── Network list click (event delegation) ─────
    $("network-list").addEventListener("click", function (e) {
      var row = e.target.closest(".network");
      if (!row) return;
      var ssid = row.getAttribute("data-ssid");
      var auth = parseInt(row.getAttribute("data-auth"), 10);

      if (auth === 0) {
        // Open network — connect directly
        performConnect(ssid, "");
      } else {
        // Secured — show password view
        selectedSSID = ssid;
        $("pwd-net-name").textContent = ssid;
        $("input-pwd").value = "";
        $("pwd-error").textContent = "";
        $("input-pwd").classList.remove("field-err");
        setStep(2);
        showView("view-password");
        setTimeout(function () { $("input-pwd").focus(); }, 100);
      }
    });

    // ── Keyboard activation for network items ─────
    $("network-list").addEventListener("keydown", function (e) {
      if (e.key === "Enter" || e.key === " ") {
        var row = e.target.closest(".network");
        if (row) { e.preventDefault(); row.click(); }
      }
    });

    // ── Hidden network button ─────────────────────
    $("btn-hidden").addEventListener("click", function () {
      $("input-manual-ssid").value = "";
      $("input-manual-pwd").value = "";
      $("ssid-error").textContent = "";
      $("input-manual-ssid").classList.remove("field-err");
      setStep(2);
      showView("view-manual");
      setTimeout(function () { $("input-manual-ssid").focus(); }, 100);
    });

    // ── Password view: Connect button ─────────────
    $("btn-connect").addEventListener("click", function () {
      var pwd = $("input-pwd").value;
      if (!pwd.trim()) {
        $("pwd-error").textContent = "Password is required";
        $("input-pwd").classList.add("field-err");
        $("input-pwd").focus();
        return;
      }
      $("pwd-error").textContent = "";
      $("input-pwd").classList.remove("field-err");
      performConnect(selectedSSID, pwd);
    });

    // Clear error on input
    $("input-pwd").addEventListener("input", function () {
      $("pwd-error").textContent = "";
      $("input-pwd").classList.remove("field-err");
    });

    // Enter key submits password
    $("input-pwd").addEventListener("keydown", function (e) {
      if (e.key === "Enter") $("btn-connect").click();
    });

    // ── Manual view: Connect button ───────────────
    $("btn-manual-connect").addEventListener("click", function () {
      var ssid = $("input-manual-ssid").value.trim();
      if (!ssid) {
        $("ssid-error").textContent = "Network name is required";
        $("input-manual-ssid").classList.add("field-err");
        $("input-manual-ssid").focus();
        return;
      }
      $("ssid-error").textContent = "";
      $("input-manual-ssid").classList.remove("field-err");
      performConnect(ssid, $("input-manual-pwd").value);
    });

    $("input-manual-ssid").addEventListener("input", function () {
      $("ssid-error").textContent = "";
      $("input-manual-ssid").classList.remove("field-err");
    });

    $("input-manual-ssid").addEventListener("keydown", function (e) {
      if (e.key === "Enter") $("input-manual-pwd").focus();
    });
    $("input-manual-pwd").addEventListener("keydown", function (e) {
      if (e.key === "Enter") $("btn-manual-connect").click();
    });

    // ── Done button (connecting view) ─────────────
    $("btn-done").addEventListener("click", function () {
      setStep(1);
      showView("view-scan");
    });

    // ── Retry button (failure) ────────────────────
    $("btn-retry").addEventListener("click", function () {
      setStep(2);
      if (selectedSSID) {
        $("pwd-net-name").textContent = selectedSSID;
        $("input-pwd").value = "";
        showView("view-password");
        setTimeout(function () { $("input-pwd").focus(); }, 100);
      } else {
        showView("view-scan");
        setStep(1);
      }
    });

    // ── Connected banner → details ────────────────
    $("connected-banner").addEventListener("click", function () {
      showView("view-details");
    });

    // ── Disconnect flow ───────────────────────────
    $("btn-disconnect").addEventListener("click", function () {
      $("modal-disconnect").style.display = "";
    });
    $("btn-cancel-dc").addEventListener("click", function () {
      $("modal-disconnect").style.display = "none";
    });
    $("btn-confirm-dc").addEventListener("click", function () {
      $("modal-disconnect").style.display = "none";
      performDisconnect();
    });

    // ── Start polling ─────────────────────────────
    refreshAP();
    startStatus();
    startRefresh();
  }

  // ── Boot ────────────────────────────────────────────
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
