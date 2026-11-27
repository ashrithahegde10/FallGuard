import express from 'express';
import cors from 'cors';
import mqtt from 'mqtt';
import dotenv from 'dotenv';

dotenv.config();
const app = express();
app.use(cors());
app.use(express.json());

const port = Number(process.env.PORT || 4000);
const deviceId = process.env.DEVICE_ID || 'device01';
const telegramBotToken = process.env.TELEGRAM_BOT_TOKEN || '';
const telegramChatId = process.env.TELEGRAM_CHAT_ID || '';
const telegramConfigured = Boolean(telegramBotToken && telegramChatId);
const root = `fallband/${deviceId}`;
const state = {
  mqttConnected: false,
  deviceOnline: false,
  sensor: null,
  status: null,
  events: [],
  lastMessageAt: null
};

async function sendTelegramAlert(message) {
  if (!telegramConfigured) {
    console.warn('Telegram alert skipped: TELEGRAM_BOT_TOKEN or TELEGRAM_CHAT_ID is not configured.');
    return false;
  }
  try {
    const response = await fetch(`https://api.telegram.org/bot${telegramBotToken}/sendMessage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: telegramChatId, text: message })
    });
    if (!response.ok) {
      const detail = await response.text();
      throw new Error(`Telegram API ${response.status}: ${detail}`);
    }
    console.log('Telegram fall alert sent.');
    return true;
  } catch (err) {
    console.error('Telegram notification failed:', err.message);
    return false;
  }
}

const client = mqtt.connect(process.env.MQTT_URL || 'mqtt://127.0.0.1:1883', {
  username: process.env.MQTT_USERNAME,
  password: process.env.MQTT_PASSWORD,
  reconnectPeriod: 3000,
  connectTimeout: 10000,
  clientId: `fallguard-backend-${Math.random().toString(16).slice(2, 8)}`
});

client.on('connect', () => {
  state.mqttConnected = true;
  client.subscribe([`${root}/sensor`, `${root}/events`, `${root}/status`]);
  console.log('Connected to MQTT broker; subscribed to', root + '/#');
});
client.on('reconnect', () => console.log('Reconnecting to MQTT...'));
client.on('close', () => { state.mqttConnected = false; });
client.on('error', err => console.error('MQTT error:', err.message));
client.on('message', (topic, buffer) => {
  let payload;
  try { payload = JSON.parse(buffer.toString()); }
  catch { console.warn('Ignoring invalid JSON on', topic); return; }
  state.lastMessageAt = new Date().toISOString();
  if (topic === `${root}/sensor`) state.sensor = payload;
  if (topic === `${root}/status`) {
    state.status = payload;
    state.deviceOnline = payload.online === true;
  }
  if (topic === `${root}/events`) {
    state.events.unshift(payload);
    state.events = state.events.slice(0, 100);
    const isFall = payload.event_type === 'fall_detected' || payload.fall_detected === true;
    if (isFall) {
      const when = payload.timestamp ? new Date(payload.timestamp * 1000).toLocaleString() : new Date().toLocaleString();
      const magnitude = typeof payload.acceleration_magnitude === 'number' ? `${payload.acceleration_magnitude.toFixed(2)} g` : 'not supplied';
      void sendTelegramAlert(`🚨 FALL DETECTED\nDevice: ${deviceId}\nTime: ${when}\nAcceleration magnitude: ${magnitude}\nCheck on the wearer immediately.`);
    }
  }
});

app.get('/api/health', (_req, res) => res.json({ api: 'ok', mqttConnected: state.mqttConnected, telegramConfigured }));
app.get('/api/state', (_req, res) => {
  // Mark readings stale if no device message has arrived recently.
  const age = state.lastMessageAt ? Date.now() - Date.parse(state.lastMessageAt) : Infinity;
  res.json({ ...state, deviceOnline: state.deviceOnline && age < 30000 });
});
app.post('/api/notifications/test', async (_req, res) => {
  if (!telegramConfigured) return res.status(400).json({ error: 'Configure TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID in backend/.env first.' });
  const sent = await sendTelegramAlert('✅ FallGuard test notification\nYour phone alerts are configured successfully.');
  if (!sent) return res.status(502).json({ error: 'Telegram could not deliver the test message. Check the bot token, chat ID and backend logs.' });
  res.json({ sent: true });
});

app.post('/api/command', (req, res) => {
  const command = req.body?.command;
  if (command !== 'reset_alarm') return res.status(400).json({ error: 'Unsupported command' });
  if (!state.mqttConnected) return res.status(503).json({ error: 'MQTT broker is disconnected' });
  client.publish(`${root}/command`, JSON.stringify({ command }), { qos: 1 }, err => {
    if (err) return res.status(500).json({ error: 'Could not publish command' });
    res.json({ sent: true, command });
  });
});

app.listen(port, '0.0.0.0', () => console.log(`FallGuard API listening on http://localhost:${port}`));
