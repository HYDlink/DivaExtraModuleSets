'use strict';

// 只读 Frida 探针：用于实机排查切换槽位与切换模型时的真实调用链、代码页保护与其他 Mod Hook。
// 严禁修改参数、寄存器、返回值或游戏内存。

const image = Process.getModuleByName('DivaMegaMix.exe');
const imageBase = image.base;

let cachedModules = null;
function refreshModules() {
    cachedModules = Process.enumerateModules().map(m => ({
        name: m.name,
        base: m.base,
        end: m.base.add(m.size),
        size: m.size,
        path: m.path
    }));
}

function resolveAddress(ptrVal) {
    if (!ptrVal || ptrVal.isNull()) return null;
    if (!cachedModules) refreshModules();
    for (const m of cachedModules) {
        if (ptrVal.compare(m.base) >= 0 && ptrVal.compare(m.end) < 0) {
            return `${m.name}+0x${ptrVal.sub(m.base).toString(16)}`;
        }
    }
    return ptrVal.toString();
}

function readPtr(addr) {
    try { return (!addr || addr.isNull()) ? null : addr.readPointer(); } catch (_) { return null; }
}
function readS32(addr) {
    try { return (!addr || addr.isNull()) ? null : addr.readS32(); } catch (_) { return null; }
}
function readU32(addr) {
    try { return (!addr || addr.isNull()) ? null : addr.readU32(); } catch (_) { return null; }
}
function readU8(addr) {
    try { return (!addr || addr.isNull()) ? null : addr.readU8(); } catch (_) { return null; }
}

function readHexBytes(addr, count) {
    try {
        const bytes = addr.readByteArray(count);
        if (!bytes) return null;
        return Array.from(new Uint8Array(bytes)).map(b => b.toString(16).padStart(2, '0')).join(' ');
    } catch (e) {
        return `ERR:${e}`;
    }
}

function decodeBranch(addr) {
    try {
        const b0 = addr.readU8();
        if (b0 === 0xE9 || b0 === 0xE8) {
            const rel = addr.add(1).readS32();
            const target = addr.add(5).add(rel);
            return {
                kind: b0 === 0xE9 ? 'jmp_rel32' : 'call_rel32',
                target: target.toString(),
                resolved: resolveAddress(target)
            };
        }
        if (b0 === 0xFF && addr.add(1).readU8() === 0x25) {
            const rel = addr.add(2).readS32();
            const ptrSlot = addr.add(6).add(rel);
            const target = readPtr(ptrSlot);
            return {
                kind: 'jmp_ff25',
                slot: ptrSlot.toString(),
                target: target ? target.toString() : null,
                resolved: target ? resolveAddress(target) : null
            };
        }
        if (b0 === 0x48 && addr.add(1).readU8() === 0xB8 && addr.add(10).readU8() === 0xFF && addr.add(11).readU8() === 0xE0) {
            const target = readPtr(addr.add(2));
            return {
                kind: 'mov_rax_jmp_rax',
                target: target ? target.toString() : null,
                resolved: target ? resolveAddress(target) : null
            };
        }
        return null;
    } catch (e) {
        return { kind: 'ERR', error: String(e) };
    }
}

// 兼容 GetSaveData 地址: 0x1D6510，如果未 Hook 则直接用 NativeFunction? 注意严禁调用可能带副作用的函数！
// 我们优先尝试读全局变量或通过 GetSaveData_via_hook 捕获 save ptr，或者 inspect 时检查 GetSaveData 字节再决定。
let observedSavePtr = null;

const TARGETS = [
    { name: 'GetSlotConfig', rva: 0x1D5E30 },
    { name: 'GetSlotConfigConst', rva: 0x1D5EC0 },
    { name: 'GetSoundConfig', rva: 0x1D5F40 },
    { name: 'GetSoundConfigTarget', rva: 0xE5B6340 },
    { name: 'GetSaveData', rva: 0x1D6510 },
    { name: 'GetModuleId', rva: 0x1D94F0 },
    { name: 'GetModuleIdTarget', rva: 0xE7DCDB0 },
    { name: 'SetModuleId', rva: 0x1D9510 },
    { name: 'SetModuleIdTarget', rva: 0xE810860 },
    { name: 'ApplySongOutfit', rva: 0x1EAD50 },
    { name: 'CycleCustomizeScope', rva: 0x681A90 },
    { name: 'GetCustomSlotWrapper', rva: 0x681CE0 },
    { name: 'CustomizeSetPreviewModule', rva: 0x68AC40 },
    { name: 'CustomizeApplyModule', rva: 0x68F430 },
    { name: 'CustomizeTaskTick', rva: 0x695250 },
    { name: 'PvSelSetScopeUi', rva: 0x6DF6D0 },
    { name: 'PvSelSetScopeUiSlow', rva: 0x6DF720 },
    { name: 'PvSelApplyUiScope', rva: 0x6EDC40 },
    { name: 'PvSelHandleF4Block', rva: 0x6F06D0 },
    { name: 'PvSelTick', rva: 0x6F4360 },
    { name: 'CommitCustomizeEquip', rva: 0x1D7FB0 },
    { name: 'ResetCustomizeEquip', rva: 0x1D8280 },
    { name: 'GetInputState', rva: 0x2AC970 }
];

const listeners = new Map();
const events = [];
const MAX_EVENTS = 4000;
let seq = 0;

function pushEvent(ev) {
    ev.seq = ++seq;
    ev.ts = Date.now();
    if (events.length < MAX_EVENTS) events.push(ev);
}

function readScopeSnapshot() {
    const controller = readPtr(imageBase.add(0xCC6F178));
    const song = readPtr(imageBase.add(0xCC5EF18));
    const member = readS32(imageBase.add(0x6EFE8C));
    const pvSelScope = (song && !song.isNull()) ? readS32(song.add(0xA8C)) : null;
    const customizeCtrlScope = (controller && !controller.isNull()) ? readS32(controller.add(0x78)) : null;
    const pvid = (song && !song.isNull() && member !== null && member >= 0x9C8 && member <= 0x100000)
        ? readS32(song.add(member - 0x9C8)) : null;
    const saveScope = (observedSavePtr && !observedSavePtr.isNull())
        ? readS32(observedSavePtr.add(0x169410)) : null;
    return {
        savePtr: observedSavePtr ? observedSavePtr.toString() : null,
        saveScope,
        customizeCtrlScope,
        pvSelScope,
        pvid
    };
}

rpc.exports = {
    inspectTargets() {
        refreshModules();
        const modList = cachedModules
            .filter(m => /Diva|ExPatch|Classics|Future|Steam|frida/i.test(m.name) || m.name === 'DivaMegaMix.exe')
            .map(m => ({ name: m.name, base: m.base.toString(), size: m.size, path: m.path }));

        const targets = TARGETS.map(t => {
            const addr = imageBase.add(t.rva);
            const range = Process.findRangeByAddress(addr);
            const bytes = readHexBytes(addr, 24);
            const branch = decodeBranch(addr);
            let nestedBranch = null;
            if (branch && branch.target && branch.kind !== 'ERR') {
                nestedBranch = {
                    bytes: readHexBytes(ptr(branch.target), 16),
                    branch: decodeBranch(ptr(branch.target))
                };
            }
            return {
                name: t.name,
                rva: '0x' + t.rva.toString(16),
                addr: addr.toString(),
                protection: range ? range.protection : null,
                rangeBase: range ? range.base.toString() : null,
                bytes,
                branch,
                nestedBranch
            };
        });

        // 同时读取 .bss / .code 等 PE 节信息
        const sections = [];
        try {
            const dos = imageBase;
            const e_lfanew = dos.add(0x3C).readS32();
            const nt = dos.add(e_lfanew);
            const numSec = nt.add(6).readU16();
            const optSize = nt.add(20).readU16();
            const secStart = nt.add(24 + optSize);
            for (let i = 0; i < numSec; i++) {
                const s = secStart.add(i * 40);
                let name = '';
                for (let j = 0; j < 8; j++) {
                    const ch = s.add(j).readU8();
                    if (ch === 0) break;
                    name += String.fromCharCode(ch);
                }
                const vsize = s.add(8).readU32();
                const va = s.add(12).readU32();
                const chrs = s.add(36).readU32();
                const range = Process.findRangeByAddress(imageBase.add(va));
                sections.push({
                    name,
                    va: '0x' + va.toString(16),
                    vsize: '0x' + vsize.toString(16),
                    characteristics: '0x' + chrs.toString(16),
                    liveProtection: range ? range.protection : null
                });
            }
        } catch (e) {
            sections.push({ error: String(e) });
        }

        return {
            imageBase: imageBase.toString(),
            modules: modList,
            sections,
            targets,
            scopeSnapshot: readScopeSnapshot()
        };
    },

    startTrace(params) {
        const names = (params && params.targets) ? params.targets : [
            'GetSlotConfig',
            'GetSlotConfigConst',
            'GetSoundConfig',
            'GetSoundConfigTarget',
            'GetSaveData',
            'GetModuleId',
            'SetModuleId',
            'ApplySongOutfit',
            'CycleCustomizeScope',
            'GetCustomSlotWrapper',
            'CustomizeSetPreviewModule',
            'CustomizeApplyModule',
            'PvSelSetScopeUi',
            'PvSelApplyUiScope',
            'CommitCustomizeEquip',
            'ResetCustomizeEquip'
        ];

        const results = {};
        for (const name of names) {
            if (listeners.has(name)) {
                results[name] = 'already_attached';
                continue;
            }
            const t = TARGETS.find(x => x.name === name);
            if (!t) {
                results[name] = 'unknown_target';
                continue;
            }
            const addr = imageBase.add(t.rva);
            try {
                const listener = Interceptor.attach(addr, {
                    onEnter(args) {
                        this._fn = name;
                        this._tid = this.threadId;
                        this._retAddr = resolveAddress(this.returnAddress);
                        this._rcx = args[0] ? args[0].toString() : null;
                        this._rdx = args[1] ? args[1].toString() : null;
                        this._r8 = args[2] ? args[2].toString() : null;
                        this._r9 = args[3] ? args[3].toString() : null;
                        // 仅在必要高频以外的函数记录前后 scope，减少开销
                        if (name !== 'GetSaveData') {
                            this._scopeBefore = readScopeSnapshot();
                        }
                    },
                    onLeave(retval) {
                        if (name === 'GetSaveData') {
                            if (retval && !retval.isNull()) observedSavePtr = ptr(retval.toString());
                            return; // GetSaveData 极高频，仅捕获 save ptr 不刷屏
                        }
                        // 对 GetSlotConfig / GetSlotConfigConst / GetModuleId / SetModuleId / 切换相关做记录
                        const ev = {
                            fn: this._fn,
                            tid: this._tid,
                            caller: this._retAddr,
                            rcx: this._rcx,
                            rdx: this._rdx,
                            r8: this._r8,
                            r9: this._r9,
                            ret: retval ? retval.toString() : null,
                            scopeBefore: this._scopeBefore,
                            scopeAfter: readScopeSnapshot()
                        };
                        if ((name === 'GetSlotConfig' || name === 'GetSlotConfigConst') && retval && !retval.isNull()) {
                            // 打印返回的 ModuleEquip 前 32 字节（只读）
                            ev.retBytes = readHexBytes(retval, 32);
                        }
                        pushEvent(ev);
                    }
                });
                listeners.set(name, listener);
                results[name] = 'attached';
            } catch (e) {
                results[name] = 'ERROR: ' + String(e);
            }
        }
        return { attachResults: results, scopeSnapshot: readScopeSnapshot() };
    },

    drainTrace() {
        const out = events.splice(0, events.length);
        return {
            count: out.length,
            scopeSnapshot: readScopeSnapshot(),
            events: out
        };
    },

    stopTrace() {
        for (const [name, listener] of listeners.entries()) {
            try { listener.detach(); } catch (_) {}
        }
        listeners.clear();
        const remaining = events.splice(0, events.length);
        return { stopped: true, drained: remaining.length };
    }
};
