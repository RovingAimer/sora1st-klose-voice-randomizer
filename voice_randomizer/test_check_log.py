"""Regression checks for log auditing; fixtures are synthetic, not game runs."""
import unittest
from check_log import summarize


def fixture():
    rows = [
        ('old', 'random_old', 0, 0, 1, 0, 1),
        ('new', 'guarantee', -1, 1, 1, 1, 0),
        ('old', 'random_old', 0, 1, 2, 0, 1),
        ('new', 'guarantee', -1, 2, 2, 1, 0),
        ('old', 'random_old', 0, 2, 3, 0, 1),
        ('new', 'guarantee', -1, 3, 3, 1, 0),
        ('new', 'capturing_radiance', 0, 3, 0, 0, 0),
    ]
    return [
        f'00:00:00.000 tid=1 PLAY event={i} voice_id=1910 name=v006_b0001 '
        f'selected={selected} queued_handle=73 mode=0 reason={reason} roll={roll} '
        f'base_old_percent=50 count_before={before} count_after={after} '
        f'guarantee_before={gb} guarantee_after={ga} drawn_selected={selected}'
        for i, (selected, reason, roll, before, after, gb, ga) in enumerate(rows, 1)
    ]


class LogAuditTests(unittest.TestCase):
    def test_reversed_target(self):
        lines = [line.replace('old', 'TEMP').replace('new', 'old').replace('TEMP', 'new')
                 .replace('roll=0', 'roll=99') +
                 ' guarantee_target=old guarantee_enabled=1 radiance_enabled=1 rules_epoch=1'
                 for line in fixture()]
        # base_old_percent is a field name, not the selected version.
        lines = [line.replace('base_new_percent', 'base_old_percent') for line in lines]
        self.assertEqual(summarize('\n'.join(lines))[2]['issues'], [])

    def test_each_switch_and_both_off(self):
        for guarantee in (0, 1):
            for radiance in (0, 1):
                # Ordinary old followed by either guarantee new or another old draw.
                first = fixture()[0].replace('count_after=1', f'count_after={radiance}')\
                    .replace('guarantee_after=1', f'guarantee_after={guarantee}')
                second = (fixture()[1].replace('count_before=1', f'count_before={radiance}')
                          .replace('count_after=1', f'count_after={radiance}') if guarantee else
                          fixture()[0].replace('event=1', 'event=2')
                          .replace('count_before=0', f'count_before={radiance}')
                          .replace('count_after=1', f'count_after={radiance*2}')
                          .replace('guarantee_after=1', 'guarantee_after=0'))
                options = (f' guarantee_target=new guarantee_enabled={guarantee}'
                           f' radiance_enabled={radiance} rules_epoch=1')
                self.assertEqual(summarize(first+options+'\n'+second+options)[2]['issues'], [])

    def test_rule_reset_epochs(self):
        first = fixture()[0] + ' guarantee_target=new guarantee_enabled=1 radiance_enabled=1 rules_epoch=0'
        second = fixture()[0].replace('event=1', 'event=2') + \
            ' guarantee_target=new guarantee_enabled=1 radiance_enabled=1 rules_epoch=2'
        self.assertEqual(summarize(first+'\n'+second)[2]['issues'], [])
        bad = second.replace('count_before=0', 'count_before=1')
        self.assertTrue(summarize(first+'\n'+bad)[2]['issues'])
        bad = second.replace('guarantee_target=new', 'guarantee_target=old').replace('rules_epoch=2', 'rules_epoch=0')
        self.assertTrue(summarize(first+'\n'+bad)[2]['issues'])

    def test_partial_new_options_rejected(self):
        self.assertTrue(summarize(fixture()[0]+' guarantee_target=old')[2]['issues'])

    def test_out_of_order_and_cache_retries(self):
        lines = fixture()[::-1] + ['CACHE name=v006_b0001 selected=old result=hit'] * 4
        plays, cache, result = summarize('\n'.join(lines))
        self.assertEqual(len(plays['v006_b0001']), 7)
        self.assertEqual(cache['v006_b0001']['old_hit'], 4)
        self.assertEqual(result['issues'], [])
        self.assertEqual(result['reasons'], dict(random_old=3, guarantee=3, capturing_radiance=1))
        self.assertEqual(result['last_state'], dict(event=7, count=0, guarantee_pending=False))

    def test_radiance_overrides_both_raw_results(self):
        lines = fixture()
        lines[-1] = lines[-1].replace('roll=0', 'roll=99')
        self.assertEqual(summarize('\n'.join(lines))[2]['issues'], [])

    def test_fallback_keeps_decision_state(self):
        lines = fixture()
        lines[0] = lines[0].replace('selected=old queued_handle', 'selected=new queued_handle')
        self.assertEqual(summarize('\n'.join(lines))[2]['issues'], [])

    def test_invalid_guarantee_and_state_gap(self):
        for wrong in ('roll=-1', 'count_before=1'):
            lines = fixture()
            lines[1] = lines[1].replace(wrong, 'roll=10' if wrong.startswith('roll') else 'count_before=2')
            self.assertTrue(summarize('\n'.join(lines))[2]['issues'])
        lines = fixture()
        del lines[2]
        self.assertTrue(summarize('\n'.join(lines))[2]['issues'])

    def test_old_format_is_not_audited(self):
        line = 'PLAY event=1 voice_id=1910 name=v006_b0001 selected=old queued_handle=73 mode=0 source=legacy'
        result = summarize(line)[2]
        self.assertEqual((result['audited'], result['unavailable'], result['issues']), (0, 1, []))

    def test_incomplete_or_invalid_rule_fields(self):
        for change in ('', 'roll=broken'):
            lines = fixture()
            lines[1] = lines[1].replace('roll=-1', change)
            self.assertTrue(summarize('\n'.join(lines))[2]['issues'])


if __name__ == '__main__':
    unittest.main()
