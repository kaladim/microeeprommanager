/*
 * mEEM EEPROM Inspector - web application UI.
 *
 * Orchestrates file selection (a dedicated tab), runs the ported business
 * logic in core.js and renders the interactive report (foldable blocks, with
 * per-value, per-block and global hex/dec switching).
 *
 * Loaded as a classic (non-module) script so it works from file://.
 */
(function () {
    'use strict';

    const E = window.EEPROM;

    // ---- The four inputs --------------------------------------------------
    const INPUTS = [
        {
            key: 'image', label: 'EEPROM image', icon: 'fa-microchip',
            accept: '.bin,.hex,.ihex,.srec,.s19,.s28,.s37,.s,.mot',
            hint: 'Raw binary (.bin), Intel HEX (.hex) or Motorola S-record (.s19)',
            binary: true,
        },
        {
            key: 'datamodel', label: 'Data model', icon: 'fa-sitemap',
            accept: '.json', hint: 'datamodel.json (mEEM configuration)', binary: false,
        },
        {
            key: 'settings', label: 'Platform settings', icon: 'fa-sliders',
            accept: '.json', hint: 'platform_settings.json (mEEM configuration)', binary: false,
        },
        {
            key: 'checksum', label: 'Checksum parameters', icon: 'fa-shield-halved',
            accept: '.json', hint: 'checksum_parameters.json', binary: false,
        },
    ];

    // key -> { name, error, and the parsed payload (bytes | obj) }
    const state = {
        image: null,
        datamodel: null,
        settings: null,
        checksum: null,
    };

    let currentModel = null;
    let globalHexMode = true; // true = HEX

    // ---- DOM refs ---------------------------------------------------------
    const fileGrid = document.getElementById('fileGrid');
    const filesMessage = document.getElementById('filesMessage');
    const reportHeader = document.getElementById('reportHeader');
    const reportRoot = document.getElementById('reportRoot');
    const reportToolbar = document.getElementById('reportToolbar');
    const reportTab = document.getElementById('reportTab');
    const viewReportBtn = document.getElementById('viewReportBtn');
    const clearBtn = document.getElementById('clearBtn');
    const tabs = document.getElementById('tabs');

    // ---- Helpers ----------------------------------------------------------
    function esc(s) {
        return String(s).replace(/[&<>"']/g, function (c) {
            return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
        });
    }

    function readFile(file, asArrayBuffer) {
        return new Promise(function (resolve, reject) {
            const r = new FileReader();
            r.onload = function () { resolve(r.result); };
            r.onerror = function () { reject(r.error || new Error('Could not read file')); };
            if (asArrayBuffer) r.readAsArrayBuffer(file);
            else r.readAsText(file);
        });
    }

    // ---- File cards -------------------------------------------------------
    function buildFileCards() {
        INPUTS.forEach(function (input) {
            const card = document.createElement('wa-card');
            card.className = 'file-card';
            card.dataset.key = input.key;
            card.innerHTML =
                '<div class="file-card-body">' +
                '  <div class="file-card-head">' +
                '    <div class="file-card-icon"><i class="fa-solid ' + input.icon + '"></i></div>' +
                '    <div class="file-card-title">' + esc(input.label) + '</div>' +
                '  </div>' +
                '  <div class="file-card-hint">' + esc(input.hint) + '</div>' +
                '  <div class="file-card-status" data-status>' +
                '    <i class="fa-regular fa-circle"></i> <span>No file selected</span>' +
                '  </div>' +
                '  <div class="file-card-action">' +
                '    <wa-button size="small" data-choose>' +
                '      <i class="fa-solid fa-file-arrow-up" slot="start"></i> Choose file' +
                '    </wa-button>' +
                '    <input type="file" accept="' + input.accept + '" hidden />' +
                '  </div>' +
                '</div>';
            fileGrid.appendChild(card);

            const fileInput = card.querySelector('input[type=file]');
            const chooseBtn = card.querySelector('[data-choose]');

            chooseBtn.addEventListener('click', function () { fileInput.click(); });
            fileInput.addEventListener('change', function () {
                if (fileInput.files && fileInput.files[0]) onFileSelected(input, fileInput.files[0]);
                fileInput.value = ''; // allow re-selecting the same file
            });

            // Drag & drop onto the card
            card.addEventListener('dragover', function (e) {
                e.preventDefault();
                card.classList.add('drag-over');
            });
            card.addEventListener('dragleave', function () { card.classList.remove('drag-over'); });
            card.addEventListener('drop', function (e) {
                e.preventDefault();
                card.classList.remove('drag-over');
                if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) {
                    onFileSelected(input, e.dataTransfer.files[0]);
                }
            });
        });
    }

    function setCardStatus(key, kind, text) {
        const card = fileGrid.querySelector('.file-card[data-key="' + key + '"]');
        if (!card) return;
        const status = card.querySelector('[data-status]');
        card.classList.remove('ok', 'error');
        let icon;
        if (kind === 'ok') { card.classList.add('ok'); icon = 'fa-solid fa-circle-check'; }
        else if (kind === 'error') { card.classList.add('error'); icon = 'fa-solid fa-circle-exclamation'; }
        else { icon = 'fa-regular fa-circle'; }
        status.innerHTML = '<i class="' + icon + '"></i> <span>' + esc(text) + '</span>';
    }

    function onFileSelected(input, file) {
        setCardStatus(input.key, 'pending', 'Reading ' + file.name + '…');
        readFile(file, input.binary).then(function (content) {
            try {
                let payload;
                if (input.key === 'image') {
                    payload = E.loadEepromImage(file.name, content);
                    state.image = { name: file.name, bytes: payload };
                    setCardStatus('image', 'ok', file.name + ' (' + payload.length + ' bytes)');
                } else if (input.key === 'datamodel') {
                    payload = E.parseDataModel(content);
                    state.datamodel = { name: file.name, obj: payload };
                    setCardStatus('datamodel', 'ok', file.name);
                } else if (input.key === 'settings') {
                    payload = E.parsePlatformSettings(content);
                    state.settings = { name: file.name, obj: payload };
                    setCardStatus('settings', 'ok', file.name);
                } else if (input.key === 'checksum') {
                    payload = JSON.parse(content);
                    state.checksum = { name: file.name, obj: payload };
                    setCardStatus('checksum', 'ok', file.name);
                }
            } catch (err) {
                state[input.key] = null;
                setCardStatus(input.key, 'error', file.name + ': ' + err.message);
            }
            tryBuild();
        }).catch(function (err) {
            state[input.key] = null;
            setCardStatus(input.key, 'error', file.name + ': ' + err.message);
            tryBuild();
        });
    }

    // ---- Build & validate -------------------------------------------------
    function showMessage(kind, html) {
        // kind: 'success' | 'danger' | 'neutral'
        filesMessage.innerHTML =
            '<wa-callout variant="' + kind + '">' +
            '<i class="fa-solid ' +
            (kind === 'success' ? 'fa-circle-check' : kind === 'danger' ? 'fa-triangle-exclamation' : 'fa-circle-info') +
            '" slot="icon"></i>' + html + '</wa-callout>';
    }

    function tryBuild() {
        const ready = state.image && state.datamodel && state.settings && state.checksum;
        if (!ready) {
            currentModel = null;
            setReportAvailable(false);
            const missing = INPUTS.filter(function (i) { return !state[i.key]; })
                .map(function (i) { return i.label; });
            if (missing.length === INPUTS.length) filesMessage.innerHTML = '';
            else showMessage('neutral', 'Waiting for: <strong>' + esc(missing.join(', ')) + '</strong>');
            return;
        }
        try {
            // Re-parse the datamodel each build: attachBlockMetadata mutates it,
            // and JSON.parse gives a fresh, clean object every time.
            const dm = JSON.parse(JSON.stringify(state.datamodel.obj));
            currentModel = E.buildReport(dm, state.settings.obj, state.image.bytes, state.checksum.obj);
            currentModel.header.image_name = state.image.name;
            currentModel.header.datamodel_name = state.datamodel.name;
            currentModel.header.settings_name = state.settings.name;
            currentModel.header.checksum_name = state.checksum.name;
            renderReport(currentModel);
            setReportAvailable(true);
            const nBlocks = currentModel.blocks.length;
            showMessage('success',
                'Report generated for <strong>' + esc(state.image.name) + '</strong> — ' +
                nBlocks + ' block' + (nBlocks === 1 ? '' : 's') + '. ' +
                'Open the <strong>Report</strong> tab or click <em>View report</em>.');
        } catch (err) {
            currentModel = null;
            setReportAvailable(false);
            showMessage('danger', 'Could not generate the report:<br><code>' + esc(err.message) + '</code>');
        }
    }

    function setReportAvailable(available) {
        if (available) {
            reportTab.removeAttribute('disabled');
            viewReportBtn.removeAttribute('disabled');
        } else {
            reportTab.setAttribute('disabled', '');
            viewReportBtn.setAttribute('disabled', '');
        }
    }

    // ---- Report rendering -------------------------------------------------
    function numWrapHtml(num) {
        return '<span class="num-wrap" data-dec="' + esc(num.value_dec) + '" data-hex="' + esc(num.value_hex) + '">' +
            '<span class="num">' + esc(num.value_hex) + '</span>' +
            '<wa-badge class="addr" appearance="outlined" variant="neutral">@ ' + esc(num.address) + '</wa-badge>' +
            '<button type="button" class="copy-btn" tabindex="-1" title="Copy value" aria-label="Copy value">' +
            '<i class="fa-regular fa-copy"></i></button>' +
            '</span>';
    }

    function instanceCellHtml(instance) {
        if (instance.data.length > 1) {
            let rows = '';
            instance.data.forEach(function (el, idx) {
                rows += '<tr><td class="idx">[' + idx + ']</td><td>' + numWrapHtml(el) + '</td></tr>';
            });
            return '<td class="array-cell"><table class="nested-table"><tbody>' + rows + '</tbody></table></td>';
        }
        return '<td class="value-cell">' + numWrapHtml(instance.data[0]) + '</td>';
    }

    function blockTableHtml(block) {
        let head = '<tr><th class="col-object">Object</th><th class="col-desc">Description</th>';
        for (let i = 0; i < block.instance_count; i++) {
            const st = block.instance_status[i];
            let badges = st.is_valid
                ? '<wa-badge variant="success">Valid</wa-badge>'
                : '<wa-badge variant="danger">Invalid</wa-badge>';
            if (st.is_most_recent) badges += ' <wa-badge variant="brand">Most recent</wa-badge>';
            head += '<th class="col-instance">Instance #' + i + ' ' + badges + '</th>';
        }
        head += '</tr>';

        let body = '';
        block.params.forEach(function (param) {
            let typeLabel = param.data_type;
            const mult = param.instances.length ? param.instances[0].data.length : 1;
            if (mult > 1) typeLabel += '[' + mult + ']';
            let rowClass = param.kind === 'checksum' || param.kind === 'sequence' ? ' class="meta-row"' : '';
            body += '<tr' + rowClass + '>';
            body += '<td class="col-object"><span class="obj-name">' + esc(param.name) + '</span>' +
                '<wa-badge class="type" appearance="outlined" variant="neutral">' + esc(typeLabel) + '</wa-badge></td>';
            body += '<td class="col-desc">' + esc(param.description) + '</td>';
            param.instances.forEach(function (inst) { body += instanceCellHtml(inst); });
            body += '</tr>';
        });

        return '<div class="table-scroll"><table class="report-table"><thead>' + head +
            '</thead><tbody>' + body + '</tbody></table></div>';
    }

    function blockHtml(block, index) {
        const totalHex = '0x' + block.total_size.toString(16).toUpperCase();
        const summary =
            '<div slot="summary" class="block-summary">' +
            '  <span class="block-title"><i class="fa-solid fa-layer-group"></i> ' + esc(block.name) + '</span>' +
            '  <span class="block-badges">' +
            '    <wa-badge variant="neutral" appearance="filled outlined">' + esc(block.management_type) + '</wa-badge>' +
            '    <wa-badge variant="neutral" appearance="outlined">' + block.instance_count +
            ' instance' + (block.instance_count === 1 ? '' : 's') + '</wa-badge>' +
            '    <span class="num-wrap block-size" data-dec="' + block.total_size + '" data-hex="' + totalHex + '">' +
            '<i class="fa-solid fa-ruler-horizontal"></i> <span class="num">' + totalHex + '</span> bytes</span>' +
            '  </span>' +
            '  <wa-button size="small" appearance="outlined" class="block-format-btn" data-block="' + index + '">' +
            '<i class="fa-solid fa-arrow-right-arrow-left" slot="start"></i>' +
            '<span data-format-label>Show all in DEC</span></wa-button>' +
            '</div>';

        const desc = block.description
            ? '<p class="block-desc"><i class="fa-regular fa-comment"></i> ' + esc(block.description) + '</p>'
            : '';

        return '<wa-details class="block" open data-block="' + index + '" data-hexmode="1">' +
            summary + desc + blockTableHtml(block) + '</wa-details>';
    }

    function headerHtml(h) {
        function row(icon, label, value) {
            return '<div class="hdr-row"><span class="hdr-label"><i class="fa-solid ' + icon + '"></i> ' +
                esc(label) + '</span><span class="hdr-value">' + value + '</span></div>';
        }
        const cs = esc(JSON.stringify(h.checksum_params));
        return '<wa-card class="report-header">' +
            row('fa-microchip', 'EEPROM image', esc(h.image_name) + ' <span class="dim">(' + h.image_size + ' bytes)</span>') +
            row('fa-sitemap', 'Data model', esc(h.datamodel_name)) +
            row('fa-sliders', 'Platform settings', esc(h.settings_name) + ' <span class="dim">(' + esc(h.endianness) + '-endian)</span>') +
            row('fa-shield-halved', 'Checksum', esc(h.checksum_name) + ' <span class="dim">(' + h.checksum_size + '-byte)</span> <code>' + cs + '</code>') +
            row('fa-clock', 'Generated', esc(h.date)) +
            '</wa-card>';
    }

    function renderReport(model) {
        globalHexMode = true;
        updateFormatLabels(reportToolbar, true);
        reportHeader.innerHTML = headerHtml(model.header);
        let html = '';
        model.blocks.forEach(function (block, i) { html += blockHtml(block, i); });
        reportRoot.innerHTML = html;
        reportToolbar.style.display = 'flex';
    }

    // ---- Hex/Dec switching ------------------------------------------------
    function setNum(wrap, hex) {
        const span = wrap.querySelector('.num');
        if (!span) return;
        span.textContent = hex ? wrap.dataset.hex : wrap.dataset.dec;
    }

    function toggleNum(wrap) {
        const span = wrap.querySelector('.num');
        if (!span) return;
        const isHex = span.textContent.trim().indexOf('0x') === 0;
        setNum(wrap, !isHex);
    }

    function updateFormatLabels(scope, hexMode) {
        const label = hexMode ? 'Show all in DEC' : 'Show all in HEX';
        scope.querySelectorAll('[data-format-label]').forEach(function (el) { el.textContent = label; });
    }

    function setScopeFormat(scope, hexMode) {
        scope.querySelectorAll('.num-wrap').forEach(function (w) { setNum(w, hexMode); });
    }

    // ---- Copy-to-clipboard for values ------------------------------------
    // Copies the value as currently displayed (hex or dec). Uses the async
    // Clipboard API when available, falling back to a hidden-textarea + execCommand
    // so it still works from a plain file:// open (no secure context).
    function legacyCopy(text) {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.setAttribute('readonly', '');
        ta.style.position = 'fixed';
        ta.style.top = '-1000px';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        let ok = false;
        try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
        document.body.removeChild(ta);
        return ok;
    }

    function copyText(text) {
        if (navigator.clipboard && navigator.clipboard.writeText) {
            return navigator.clipboard.writeText(text).then(
                function () { return true; },
                function () { return legacyCopy(text); });
        }
        return Promise.resolve(legacyCopy(text));
    }

    function flashCopied(btn) {
        const icon = btn.querySelector('i');
        btn.classList.add('copied');
        if (icon) icon.className = 'fa-solid fa-check';
        clearTimeout(btn._copyTimer);
        btn._copyTimer = setTimeout(function () {
            btn.classList.remove('copied');
            if (icon) icon.className = 'fa-regular fa-copy';
        }, 1200);
    }

    // Delegated click handling for the whole report.
    //
    // The per-block format button and the block-size badge both live inside the
    // <wa-details> summary. In WebAwesome 3 that summary is a NATIVE <details>/
    // <summary> in the component's shadow root, so a summary click can collapse
    // the block via two independent paths:
    //   1. the browser's native <details> toggle (a default action), and
    //   2. wa-details' own handleSummaryClick(), which animates open/closed.
    // Neutralising a control click therefore needs BOTH:
    //   - preventDefault(): cancels the native <details> default toggle
    //     (stopPropagation cannot - default actions are not propagation), and
    //   - stopPropagation() in the CAPTURE phase: reportRoot is an ancestor of
    //     every wa-details, so this runs before the shadow <summary>'s
    //     bubble-phase click listener and keeps handleSummaryClick from firing.
    // Expand/collapse then stays governed solely by the disclosure (">") button.
    function suppressSummaryToggle(e) {
        e.preventDefault();
        e.stopPropagation();
    }
    reportRoot.addEventListener('click', function (e) {
        const copyBtn = e.target.closest && e.target.closest('.copy-btn');
        if (copyBtn) {
            // Copy must not also toggle hex/dec on the sibling value.
            e.stopPropagation();
            const wrap = copyBtn.closest('.num-wrap');
            const numSpan = wrap && wrap.querySelector('.num');
            if (numSpan) {
                copyText(numSpan.textContent.trim()).then(function (ok) {
                    if (ok) flashCopied(copyBtn);
                });
            }
            return;
        }
        const numSpan = e.target.closest && e.target.closest('.num');
        if (numSpan) {
            const wrap = numSpan.closest('.num-wrap');
            if (wrap) { toggleNum(wrap); suppressSummaryToggle(e); }
            return;
        }
        const btn = e.target.closest && e.target.closest('.block-format-btn');
        if (btn) {
            suppressSummaryToggle(e); // keep the block from folding
            const details = btn.closest('.block');
            const hexMode = details.dataset.hexmode !== '1'; // flip
            details.dataset.hexmode = hexMode ? '1' : '0';
            setScopeFormat(details, hexMode);
            updateFormatLabels(details, hexMode);
        }
    }, true); // capture phase - see comment above

    // ---- Report toolbar ---------------------------------------------------
    reportToolbar.addEventListener('click', function (e) {
        if (e.target.closest('#expandAllBtn')) {
            reportRoot.querySelectorAll('wa-details.block').forEach(function (d) { d.open = true; });
        } else if (e.target.closest('#collapseAllBtn')) {
            reportRoot.querySelectorAll('wa-details.block').forEach(function (d) { d.open = false; });
        } else if (e.target.closest('#globalFormatBtn')) {
            globalHexMode = !globalHexMode;
            setScopeFormat(reportRoot, globalHexMode);
            updateFormatLabels(reportToolbar, globalHexMode);
            // keep per-block state in sync
            reportRoot.querySelectorAll('wa-details.block').forEach(function (d) {
                d.dataset.hexmode = globalHexMode ? '1' : '0';
                updateFormatLabels(d, globalHexMode);
            });
        }
    });

    // ---- Tab switching ----------------------------------------------------
    // WebAwesome's <wa-tab-group> exposes the active tab via its `active`
    // property/attribute (the panel name) - there is no show() method.
    function activateTab(name) {
        customElements.whenDefined('wa-tab-group').then(function () { tabs.active = name; });
    }
    function activateReportTab() {
        if (reportTab.hasAttribute('disabled')) return;
        activateTab('report');
    }
    viewReportBtn.addEventListener('click', activateReportTab);

    // ---- Clear ------------------------------------------------------------
    clearBtn.addEventListener('click', function () {
        Object.keys(state).forEach(function (k) { state[k] = null; });
        currentModel = null;
        INPUTS.forEach(function (i) { setCardStatus(i.key, 'none', 'No file selected'); });
        filesMessage.innerHTML = '';
        reportHeader.innerHTML = '';
        reportRoot.innerHTML =
            '<div class="report-empty"><i class="fa-regular fa-file-lines"></i>' +
            '<p>No report yet. Load the input files to generate one.</p></div>';
        reportToolbar.style.display = 'none';
        setReportAvailable(false);
        activateTab('files');
    });

    // ---- Init -------------------------------------------------------------
    buildFileCards();
    // Activate the first tab once the component has rendered. The active panel
    // is set from JS (not via an HTML attribute) so the property genuinely
    // changes and the tab-group renders/shows the panel.
    customElements.whenDefined('wa-tab-group').then(function () {
        var apply = function () { tabs.active = 'files'; };
        if (tabs.updateComplete) tabs.updateComplete.then(apply); else apply();
    });
})();
