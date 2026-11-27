import { useEffect, useState } from 'react';

type Sensor = { accel_x?: number; accel_y?: number; accel_z?: number; gyro_x?: number; gyro_y?: number; gyro_z?: number; acceleration_magnitude?: number; fall_detected?: boolean; buzzer?: boolean; timestamp?: number };
type EventItem = { event_id?: string; event_type?: string; timestamp?: number; acceleration_magnitude?: number };
type State = { mqttConnected: boolean; deviceOnline: boolean; sensor: Sensor | null; status: Record<string, unknown> | null; events: EventItem[]; lastMessageAt: string | null };
const API = import.meta.env.VITE_API_URL || 'http://localhost:4000';
const empty: State = { mqttConnected: false, deviceOnline: false, sensor: null, status: null, events: [], lastMessageAt: null };
const fmt = (n: unknown, digits = 2) => typeof n === 'number' ? n.toFixed(digits) : '—';

export default function App() {
  const [data, setData] = useState<State>(empty);
  const [error, setError] = useState('');
  const [sending, setSending] = useState(false);
  const [testingNotification, setTestingNotification] = useState(false);
  const [notificationMessage, setNotificationMessage] = useState('');
  useEffect(() => {
    let active = true;
    const load = async () => {
      try { const r = await fetch(`${API}/api/state`); if (!r.ok) throw new Error('Backend unavailable'); const json = await r.json(); if (active) { setData(json); setError(''); } }
      catch { if (active) setError('Cannot reach backend. Start the Node.js server on port 4000.'); }
    };
    load(); const id = setInterval(load, 1000);
    return () => { active = false; clearInterval(id); };
  }, []);
  const s = data.sensor;
  const fall = s?.fall_detected === true;
  async function testPhoneNotification() {
    setTestingNotification(true);
    setNotificationMessage('');
    try {
      const r = await fetch(`${API}/api/notifications/test`, { method: 'POST' });
      const body = await r.json();
      if (!r.ok) throw new Error(body.error || 'Test notification failed');
      setNotificationMessage('Test alert sent. Check your Telegram chat.');
    } catch (e) {
      setNotificationMessage(e instanceof Error ? e.message : 'Could not send test alert.');
    } finally { setTestingNotification(false); }
  }
  async function resetAlarm() {
    setSending(true);
    try { const r = await fetch(`${API}/api/command`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ command: 'reset_alarm' }) }); if (!r.ok) throw new Error(); }
    catch { setError('Could not send reset command. Check MQTT and backend connection.'); }
    finally { setSending(false); }
  }
  return <main className="shell">
    <header><div><div className="eyebrow">WEARABLE IOT MONITORING</div><h1>FallGuard</h1><p>Fall detection · Live device telemetry</p></div><div className="connection"><i className={data.mqttConnected ? 'dot good' : 'dot'} /> MQTT {data.mqttConnected ? 'Connected' : 'Disconnected'}</div></header>
    {error && <div className="error">{error}</div>}
    <section className="hero"><div><span className="eyebrow">CURRENT SAFETY STATUS</span><h2 className={fall ? 'dangerText' : ''}>{fall ? 'FALL DETECTED' : 'No fall detected'}</h2><p>{data.deviceOnline ? 'Device is reporting in' : 'Waiting for device data'}</p></div><div className={`statusPill ${data.deviceOnline ? 'online' : ''}`}>{data.deviceOnline ? 'DEVICE ONLINE' : 'DEVICE OFFLINE'}</div></section>
    <section className="grid">
      <Metric label="Acceleration magnitude" value={fmt(s?.acceleration_magnitude)} unit="g" />
      <Metric label="Buzzer" value={s?.buzzer ? 'ON' : s ? 'OFF' : '—'} unit="" />
      <Metric label="Fall events" value={String(data.events.length)} unit="recorded" />
      <Metric label="Last message" value={data.lastMessageAt ? new Date(data.lastMessageAt).toLocaleTimeString() : '—'} unit="" />
    </section>
    <section className="panel"><div className="panelHead"><div><h3>Live sensor readings</h3><p>Latest MPU6050 values</p></div><span className="live">● LIVE</span></div><div className="readings">{[['Accelerometer X',s?.accel_x,'g'],['Accelerometer Y',s?.accel_y,'g'],['Accelerometer Z',s?.accel_z,'g'],['Gyroscope X',s?.gyro_x,'°/s'],['Gyroscope Y',s?.gyro_y,'°/s'],['Gyroscope Z',s?.gyro_z,'°/s']].map(([label,value,unit]) => <div className="reading" key={String(label)}><span>{label}</span><b>{fmt(value)} <small>{unit}</small></b></div>)}</div></section>
    <section className="panel"><div className="panelHead"><div><h3>Fall event history</h3><p>Most recent events received over MQTT</p></div><div className="actions"><button className="secondaryButton" onClick={testPhoneNotification} disabled={testingNotification}>{testingNotification ? 'Sending…' : 'Test phone alert'}</button><button onClick={resetAlarm} disabled={sending || !data.mqttConnected}>{sending ? 'Sending…' : 'Reset alarm'}</button></div></div>{notificationMessage && <div className="notice" role="status">{notificationMessage}</div>}{data.events.length ? <div className="events">{data.events.map((e,i)=><div className="event" key={e.event_id || i}><span className="eventIcon">!</span><div><b>{e.event_type || 'Fall event'}</b><p>{e.timestamp ? new Date(e.timestamp * 1000).toLocaleString() : 'Timestamp unavailable'} · Magnitude {fmt(e.acceleration_magnitude)} g</p></div></div>)}</div> : <div className="empty">No fall events received yet.</div>}</section>
    <footer>Prototype dashboard · Not a medical or emergency-response system</footer>
  </main>;
}
function Metric({label,value,unit}:{label:string;value:string;unit:string}) { return <div className="metric"><span>{label}</span><strong>{value}</strong><small>{unit}</small></div>; }
