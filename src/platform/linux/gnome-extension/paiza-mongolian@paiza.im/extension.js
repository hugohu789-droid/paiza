// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Traditional vertical Mongolian candidates for GNOME Shell's IBus
// popup. On Wayland the Paiza engine cannot draw its own candidate
// window (see docs/linux-port.md), so the shell's popup does the
// rendering — but it lays every word out horizontally. While the
// paiza-mongolian engine is active, this extension:
//
//   * flows the candidate boxes left-to-right (columns) inside a row,
//   * stacks each box vertically: selection number above the word,
//   * rotates each word 90 degrees clockwise — the same transform the
//     Windows/macOS/Linux-Xorg Paiza candidate windows use, turning
//     horizontally shaped glyphs into top-to-bottom Mongolian,
//   * compacts the theme's roomy candidate-box padding (12px each side)
//     so columns hug their words,
//   * moves the paging controls into a footer row below the candidates,
//     bottom-right, with a "page/pages" label — the Windows/macOS
//     candidate-window footer.
//
// Every other input method sees the stock popup. Disabling the
// extension restores the shell's own widgets and methods.

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import IBus from 'gi://IBus';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import {getIBusManager} from 'resource:///org/gnome/shell/misc/ibusManager.js';
import {
    getInputSourceManager,
    INPUT_SOURCE_TYPE_IBUS,
} from 'resource:///org/gnome/shell/ui/status/keyboard.js';

const ENGINE_ID = 'paiza-mongolian';

// Compact overrides for the theme's .candidate-box padding (6px 12px)
// and .candidate-index padding-right (6px), which read as huge gaps
// between vertical columns.
const COMPACT_BOX_STYLE = 'padding: 4px 3px;';
const COMPACT_INDEX_STYLE = 'padding: 0;';
const FOOTER_STYLE = 'height: 1.6em; padding: 3px 0 0 0; spacing: 4px;';
const PAGE_LABEL_STYLE = 'color: #9b9b9d; font-size: 90%; padding: 0 4px;';

// Hosts one candidate label. When vertical, reports the child's size
// with width/height swapped and draws it rotated 90° clockwise (glyph
// flow becomes top-to-bottom, glyph tops face right — correct for
// traditional Mongolian).
const RotatedBin = GObject.registerClass(
class PaizaRotatedBin extends Clutter.Actor {
    _init(child) {
        super._init();
        this._child = child;
        this._vertical = false;
        this.add_child(child);
    }

    setVertical(vertical) {
        if (this._vertical === vertical)
            return;
        this._vertical = vertical;
        this.queue_relayout();
    }

    vfunc_get_preferred_width(forHeight) {
        return this._vertical
            ? this._child.get_preferred_height(-1)
            : this._child.get_preferred_width(forHeight);
    }

    vfunc_get_preferred_height(forWidth) {
        return this._vertical
            ? this._child.get_preferred_width(-1)
            : this._child.get_preferred_height(forWidth);
    }

    vfunc_allocate(box) {
        this.set_allocation(box);
        const [, natW] = this._child.get_preferred_width(-1);
        const [, natH] = this._child.get_preferred_height(-1);
        const childBox = new Clutter.ActorBox();
        childBox.set_origin(0, 0);
        childBox.set_size(natW, natH);
        this._child.allocate(childBox);
        if (this._vertical) {
            // Clockwise: (x, y) -> (-y, x); shift right so the rotated
            // glyph box lands in [0, natH] x [0, natW].
            this._child.set_pivot_point(0, 0);
            this._child.rotation_angle_z = 90;
            this._child.translation_x = natH;
        } else {
            this._child.rotation_angle_z = 0;
            this._child.translation_x = 0;
        }
    }
});

export default class PaizaMongolianExtension extends Extension {
    enable() {
        this._sourceManager = getInputSourceManager();
        this._popup = getIBusManager()._candidatePopup;
        this._area = this._popup?._candidateArea;
        if (!this._area?._candidateBoxes)
            throw new Error('IBus candidate area not found');
        this._row = null;
        this._pageLabel = null;
        this._active = false;

        // Wrap each candidate label in a rotator (index label stays
        // horizontal — it is the "1"–"9" shortcut digit).
        for (const box of this._area._candidateBoxes) {
            const label = box._candidateLabel;
            box.remove_child(label);
            box._paizaBin = new RotatedBin(label);
            box.insert_child_at_index(box._paizaBin, 1);
        }

        // Re-apply our layout after every candidate update, and force
        // horizontal semantics (left/right paging icons) while Paiza is
        // active. Instance properties shadow the prototype methods;
        // `delete` restores.
        const self = this;
        const area = this._area;
        area.setCandidates = function (...args) {
            Object.getPrototypeOf(this).setCandidates.apply(this, args);
            self._applyLayout();
        };
        area.setOrientation = function (orientation) {
            Object.getPrototypeOf(this).setOrientation.call(
                this,
                self._active ? IBus.Orientation.HORIZONTAL : orientation);
        };

        // Footer page label ("2/5"), fed by the panel service's lookup
        // table updates. The service can be replaced on ibus restarts,
        // so shadow setPanelService to re-attach.
        this._lookupTableHandler = [null, 0];
        this._connectPanelService(this._popup._panelService);
        const popup = this._popup;
        popup.setPanelService = function (panelService) {
            Object.getPrototypeOf(this).setPanelService.call(
                this, panelService);
            self._connectPanelService(panelService);
        };

        this._sourceChangedId = this._sourceManager.connect(
            'current-source-changed', () => this._updateActive());
        this._updateActive();
    }

    disable() {
        if (this._sourceChangedId) {
            this._sourceManager.disconnect(this._sourceChangedId);
            this._sourceChangedId = 0;
        }
        this._disconnectPanelService();
        if (this._popup)
            delete this._popup.setPanelService;
        if (this._area) {
            // Restore the stock single-row structure and styles first.
            this._active = false;
            this._applyLayout();
            delete this._area.setCandidates;
            delete this._area.setOrientation;
            this._area._orientation = -1;  // Re-sync on next update.
            for (const box of this._area._candidateBoxes ?? []) {
                const bin = box._paizaBin;
                if (!bin)
                    continue;
                const label = box._candidateLabel;
                bin.remove_child(label);
                box.remove_child(bin);
                bin.destroy();
                delete box._paizaBin;
                label.rotation_angle_z = 0;
                label.translation_x = 0;
                box.insert_child_at_index(label, 1);
            }
        }
        this._area = null;
        this._popup = null;
        this._sourceManager = null;
    }

    _connectPanelService(panelService) {
        this._disconnectPanelService();
        if (!panelService)
            return;
        const id = panelService.connect(
            'update-lookup-table', (_ps, lookupTable, visible) => {
                this._updatePageLabel(lookupTable, visible);
            });
        this._lookupTableHandler = [panelService, id];
    }

    _disconnectPanelService() {
        const [service, id] = this._lookupTableHandler ?? [null, 0];
        if (service && id)
            service.disconnect(id);
        this._lookupTableHandler = [null, 0];
    }

    _updatePageLabel(lookupTable, visible) {
        if (!this._pageLabel)
            return;
        const nCandidates = lookupTable.get_number_of_candidates();
        const pageSize = lookupTable.get_page_size();
        const nPages = Math.ceil(nCandidates / pageSize);
        const page = Math.floor(lookupTable.get_cursor_pos() / pageSize);
        this._pageLabel.text = `${page + 1}/${nPages}`;
        this._pageLabel.visible = visible && this._active && nPages > 1;
    }

    _updateActive() {
        const source = this._sourceManager.currentSource;
        this._active = source != null &&
            source.type === INPUT_SOURCE_TYPE_IBUS &&
            source.id === ENGINE_ID;
        // Bust setOrientation's early-return cache so the forced (or
        // restored) icons/style are applied on the next update.
        this._area._orientation = -1;
        this._applyLayout();
    }

    _setBoxVertical(box, vertical) {
        // St.BoxLayout: `orientation` since GNOME 48, `vertical` before.
        if (box.orientation !== undefined) {
            box.orientation = vertical
                ? Clutter.Orientation.VERTICAL
                : Clutter.Orientation.HORIZONTAL;
        } else {
            box.vertical = vertical;
        }
    }

    // Candidates row above, paging footer below (bottom-right) — the
    // Windows/macOS candidate-window arrangement.
    _restructure(active) {
        const area = this._area;
        if (active && !this._row) {
            this._row = new St.BoxLayout();
            area.insert_child_at_index(this._row, 0);
            for (const box of area._candidateBoxes) {
                area.remove_child(box);
                this._row.add_child(box);
            }
            area._buttonBox.x_align = Clutter.ActorAlign.END;
            area._buttonBox.style = FOOTER_STYLE;
            this._pageLabel = new St.Label({
                style: PAGE_LABEL_STYLE,
                y_align: Clutter.ActorAlign.CENTER,
                visible: false,
            });
            area._buttonBox.insert_child_at_index(this._pageLabel, 0);
        } else if (!active && this._row) {
            for (let i = 0; i < area._candidateBoxes.length; ++i) {
                const box = area._candidateBoxes[i];
                this._row.remove_child(box);
                area.insert_child_at_index(box, i);
            }
            area.remove_child(this._row);
            this._row.destroy();
            this._row = null;
            area._buttonBox.remove_child(this._pageLabel);
            this._pageLabel.destroy();
            this._pageLabel = null;
            area._buttonBox.x_align = Clutter.ActorAlign.FILL;
            area._buttonBox.style = null;
        }
    }

    _applyLayout() {
        const area = this._area;
        this._restructure(this._active);
        if (this._active) {
            // The shadowed setOrientation forces HORIZONTAL (left/right
            // paging icons + horizontal style class); stack our row and
            // the footer vertically regardless.
            area.orientation = Clutter.Orientation.VERTICAL;
        }
        for (const box of area._candidateBoxes) {
            const bin = box._paizaBin;
            if (!bin)
                continue;
            bin.setVertical(this._active);
            this._setBoxVertical(box, this._active);
            box.style = this._active ? COMPACT_BOX_STYLE : null;
            box._indexLabel.style = this._active
                ? COMPACT_INDEX_STYLE : null;
            box._indexLabel.x_align = this._active
                ? Clutter.ActorAlign.CENTER
                : Clutter.ActorAlign.START;
            bin.x_align = this._active
                ? Clutter.ActorAlign.CENTER
                : Clutter.ActorAlign.START;
        }
    }
}
