// 云函数：collectSensorData —— 定时采集 T-Watch 传感器数据（免费应用端 API）
// 触发：每分钟（config.json 定时触发器）
// 流程：GET ThingsCloud 应用端 API 属性当前值 → 写入云数据库 health_records
// 凭据说明：AccessToken / Project-Key 来自 ThingsCloud 设备证书（应用端设备访问 API），免费版可用。
//          这些凭据只存在云端云函数里，小程序前端拿不到，安全。

const cloud = require('wx-server-sdk')
cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })
const https = require('https')

// ===== ThingsCloud 配置（设备证书，免费版应用端 API）=====
const API_ENDPOINT = 'https://gz-3-api.iot-api.com'      // 应用端 API 接入点
const ACCESS_TOKEN = 'YOUR_ACCESS_TOKEN'                  // 设备证书 AccessToken
const PROJECT_KEY  = 'YOUR_PROJECT_KEY'                        // 设备证书 Project-Key

// 请求 ThingsCloud 应用端 API：获取属性当前值
function fetchAttributes() {
  return new Promise((resolve, reject) => {
    const url = `${API_ENDPOINT}/app/device/v1/${ACCESS_TOKEN}/attributes`
    const req = https.get(url, {
      headers: {
        'Content-Type': 'application/json',
        'Project-Key': PROJECT_KEY
      }
    }, (res) => {
      let data = ''
      res.on('data', (chunk) => { data += chunk })
      res.on('end', () => {
        try {
          resolve(JSON.parse(data))
        } catch (e) {
          reject(new Error('JSON parse error: ' + data.slice(0, 200)))
        }
      })
    })
    req.on('error', reject)
    req.setTimeout(10000, () => req.destroy(new Error('request timeout')))
  })
}

exports.main = async (event, context) => {
  try {
    const resp = await fetchAttributes()
    if (!resp || resp.result !== 1 || !resp.attributes) {
      return { ok: false, msg: 'API result != 1', resp }
    }
    const a = resp.attributes
    const record = {
      ts: Date.now(),
      temp: a.temp != null ? Number(a.temp) : null,
      hum: a.hum != null ? Number(a.hum) : null,
      press: a.press != null ? Number(a.press) : null,
      bpm: a.bpm != null ? Number(a.bpm) : null,
      spo2: a.spo2 != null ? Number(a.spo2) : null,
      uv: a.uv != null ? Number(a.uv) : null,
      rssi: a.rssi != null ? Number(a.rssi) : null,
      uptime: a.uptime != null ? Number(a.uptime) : null,
      online: a.online != null ? Number(a.online) : null,
      steps: a.steps != null ? Number(a.steps) : null,
      dist: a.dist != null ? Number(a.dist) : null,
      cal: a.cal != null ? Number(a.cal) : null
    }

    // 写入云数据库（集合 health_records）
    const db = cloud.database()
    const addRes = await db.collection('health_records').add({ data: record })
    return { ok: true, id: addRes._id, record }
  } catch (err) {
    console.error('collectSensorData error:', err)
    return { ok: false, msg: err.message }
  }
}
