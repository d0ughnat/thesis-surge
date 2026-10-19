import csv,json
from pathlib import Path
fixture=json.loads(Path('build/trial-fixture.json').read_text())
with open('build/trial-fixture.csv',newline='') as source:
    rows=list(csv.DictReader(source))
assert len(rows)==1
row=rows[0]
assert None not in row and len(row)==25
assert row['session']==fixture['session']
assert int(row['trial'])==fixture['number']
assert row['result']==fixture['result']
assert row['epoch']=='' and row['clock_valid']=='false'
assert row['notes']=="'"+fixture['notes']
assert row['event_id']==fixture['first_event']['id']
assert float(row['rms_v'])==fixture['first_event']['v']
assert int(row['response_us'])==fixture['first_event']['response_us']
assert row['physical_observation']==fixture['physical_observation']
print('PASS: production CSV/JSON agreement, nullable unsynchronized clock, quoting/newlines, formula prefix escaping')
