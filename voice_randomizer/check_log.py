"""Summarize queue requests and audit rule transitions in selection order."""
from pathlib import Path
from collections import defaultdict, Counter
import json
import re
import sys


def audit_rules(events):
    ordered = sorted(events, key=lambda e: e['event'])
    reasons = Counter()
    issues = []
    previous = None
    audited = 0
    required = ('reason', 'roll', 'base_old_percent', 'count_before', 'count_after',
                'guarantee_before', 'guarantee_after', 'drawn_selected', 'mode')
    options = ('guarantee_target', 'guarantee_enabled', 'radiance_enabled', 'rules_epoch')
    for event in ordered:
        if not any(key in event for key in required[:-1]):
            previous = None  # Older logs cannot validate these rules.
            continue
        number = event['event']
        if not all(key in event for key in required) or (
                any(key in event for key in options) and not all(key in event for key in options)):
            issues.append(f'event={number}: 规则字段不完整')
            previous = None
            continue
        try:
            roll, percent, before, after, gb, ga, mode = (
                int(event[key]) for key in required[1:7] + ('mode',))
            target = event.get('guarantee_target', 'new')
            guarantee = int(event.get('guarantee_enabled', 1))
            radiance = int(event.get('radiance_enabled', 1))
            epoch = int(event.get('rules_epoch', 0))
        except ValueError:
            issues.append(f'event={number}: 规则数值无法解析')
            previous = None
            continue
        audited += 1
        reasons[event['reason']] += 1
        problems = []
        if not (0 <= before <= 3 and 0 <= after <= 3 and gb in (0, 1)
                and ga in (0, 1) and 0 <= percent <= 100 and target in ('new', 'old')
                and guarantee in (0, 1) and radiance in (0, 1) and epoch >= 0):
            problems.append('计数、保底或概率超出范围')
        if (not guarantee and (gb or ga)) or (not radiance and (before or after)):
            problems.append('已关闭的机制仍然保留状态')
        if number == 1 and (before, gb) != (0, 0):
            problems.append('初始状态不为零')
        if previous:
            if number != previous[0] + 1:
                problems.append('事件号缺失或重复，无法确认连续性')
            elif epoch < previous[3]:
                problems.append('配置代数倒退')
            elif epoch > previous[3]:
                if (before, gb) != (0, 0):
                    problems.append('配置变化后状态没有清零')
            elif (target, guarantee, radiance) != previous[4]:
                problems.append('规则变化但未更新配置代数')
            elif (before, gb) != previous[1:3]:
                problems.append('状态与上次结果不连续')
        expected_count, expected_guarantee = before, gb
        if mode == 0:
            if guarantee and gb:
                reason, selected = 'guarantee', target
                expected_guarantee = 0
                if roll != -1:
                    problems.append('保底进行了随机抽取')
            else:
                if not 0 <= roll <= 99:
                    problems.append('随机值不在 0～99')
                if radiance and before >= 3:
                    reason, selected = 'capturing_radiance', target
                    expected_count = 0
                else:
                    selected = 'old' if roll < percent else 'new'
                    reason = 'random_' + selected
                    missed = selected != target
                    expected_guarantee = int(bool(guarantee and missed))
                    if radiance:
                        expected_count = before + 1 if missed else max(0, before - 1)
        else:
            reason = {1: 'alternate', 2: 'forced_new', 3: 'forced_old'}.get(mode)
            selected = {2: 'new', 3: 'old'}.get(mode, event['drawn_selected'])
            if roll != -1:
                problems.append('调试模式进行了随机抽取')
        if (event['reason'], event['drawn_selected'], after, ga) != (
                reason, selected, expected_count, expected_guarantee):
            problems.append('选择原因、分支或状态不符合规则')
        issues.extend(f'event={number}: {problem}' for problem in problems)
        previous = (number, after, ga, epoch, (target, guarantee, radiance))
    return dict(audited=audited, unavailable=len(ordered)-audited,
                reasons=dict(reasons), issues=issues,
                last_state=(dict(event=previous[0], count=previous[1],
                                 guarantee_pending=bool(previous[2])) if previous else None))


def summarize(log_text):
    plays = defaultdict(list)
    cache = defaultdict(Counter)
    for line in log_text.splitlines():
        match = re.search(r'PLAY event=(\d+) voice_id=(\d+) name=(\S+) selected=(new|old) queued_handle=(\d+)', line)
        if match:
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
            fields.update(event=int(match[1]), voice_id=int(match[2]), handle=int(match[5]))
            plays[match[3]].append(fields)
        match = re.search(r'CACHE name=(\S+) selected=(new|old) result=(hit|miss)', line)
        if match:
            cache[match[1]][match[2]+'_'+match[3]] += 1
    return plays, cache, audit_rules([e for events in plays.values() for e in events])


def main():
    root = Path(__file__).resolve().parent
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else root/'voice_randomizer.log'
    if not path.exists():
        print('尚无游戏运行日志。请从 Steam 启动游戏，播放科洛丝的剧情或其他已覆盖语音。')
        return 0
    log_text = path.read_text(encoding='utf-8', errors='replace')
    if 'READY ' not in log_text:
        print('未发现 READY；插件尚未完成加载，或当前 EXE/素材未通过检查。')
        print('\n'.join(log_text.splitlines()[-15:]))
        return 1
    plays, cache, rules = summarize(log_text)
    print('插件已加载。PLAY 记录表示提交请求，实际听感与演出同步仍需实机确认。')
    version = re.search(r'INIT Voice Randomizer(?: prototype)? ([^;]+)', log_text)
    print('日志版本：' + (version[1] if version else '未知'))
    set_match = re.search(r'ASSET_SET source=(\w+)', log_text)
    payload = set_match[1] if set_match else 'legacy'
    print('替换分支素材：'+('合成占位语音（提示音＋中文测试声）' if payload == 'placeholder' else '旧版配音'))
    print('语音名                 游戏请求 替换请求 游戏缓存命中 替换缓存命中')
    summary = []
    for name, events in sorted(plays.items()):
        accepted = [x for x in events if x['handle'] != 4294967295]
        selected = Counter(x['selected'] for x in accepted)
        c = cache[name]
        print(f"{name:22s} {selected['new']:7d} {selected['old']:7d} {c['new_hit']:12d} {c['old_hit']:12d}")
        summary.append(dict(name=name, replacement_source=payload, queued=dict(selected),
                            rejected=len(events)-len(accepted), cache=dict(c)))
    if not plays:
        print('还没有命中素材清单内语音的播放请求。')
    else:
        all_events = [e for events in plays.values() for e in events]
        total = Counter(x['selected'] for x in all_events if x['handle'] != 4294967295)
        rejected = sum(x['handle'] == 4294967295 for x in all_events)
        print(f"合计：游戏 {total['new']}，替换 {total['old']}；排队失败 {rejected}；回退日志 {log_text.count('FALLBACK')}。")
    if rules['audited']:
        counts = Counter(rules['reasons'])
        print(f"规则选择：随机新 {counts['random_new']}，随机旧 {counts['random_old']}，保底 {counts['guarantee']}，捕获明光 {counts['capturing_radiance']}。")
        print(f"按 event 排序检查 {rules['audited']} 次状态转换；异常 {len(rules['issues'])}。")
        if rules['last_state']:
            state = rules['last_state']
            print(f"最后计数 {state['count']}；待触发保底：{'是' if state['guarantee_pending'] else '否'}。")
        print('OldPercent 始终表示基础旧版概率；开启保底或捕获明光后，最终比例会向 GuaranteeTarget 倾斜。')
    if rules['unavailable']:
        print(f"{rules['unavailable']} 次请求没有完整规则字段，不能据此验证两层机制（旧版本日志不含这些字段）。")
    for issue in rules['issues']:
        print('异常：' + issue)
    (root/'runtime_summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (root/'runtime_rules_summary.json').write_text(json.dumps(dict(log=str(path), **rules), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return 1 if rules['issues'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
