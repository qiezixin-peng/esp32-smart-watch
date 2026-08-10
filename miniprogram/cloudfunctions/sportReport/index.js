// 云函数：sportReport —— 运动报表数据聚合（当天24小时增量 + 最近7天每日总量）
// 流程：查最近 7 天 health_records → 按小时聚合步数增量 + 按天聚合每日总量
// 说明：
//   - 数据库存的是"当日累计步数"（手表 BMA423 计步，跨天归零）
//   - 24h 柱状图用增量（5 点走 28 步、6 点没走就是 0，不延续 28）
//   - 7 天柱状图用每天最后一条累计值 = 当天总步数

const cloud = require("wx-server-sdk")
cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })

exports.main = async (event, context) => {
  const db = cloud.database()
  const _ = db.command

  const now = new Date()
  const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0, 0)
  const startTs = startOfToday.getTime()
  const DAY_MS = 86400000
  const start7 = startTs - 6 * DAY_MS   // 7 天前 0 点

  // 分页拉取 7 天内记录
  const PAGE = 1000
  let records = []
  let skip = 0
  while (true) {
    const res = await db.collection("health_records")
      .where({ ts: _.gte(start7) })
      .orderBy("ts", "asc")
      .skip(skip)
      .limit(PAGE)
      .get()
    records = records.concat(res.data || [])
    if (!res.data || res.data.length < PAGE) break
    skip += PAGE
  }

  // 只保留含步数的记录
  const withSteps = records.filter(r => r.steps != null)

  // ===== 当天 24 小时增量 =====
  const lastPerHour = new Array(24).fill(null)
  for (const r of withSteps) {
    if (r.ts < startTs) continue      // 只算今天
    const h = new Date(r.ts).getHours()
    lastPerHour[h] = Number(r.steps)
  }
  const hourly = new Array(24).fill(0)
  let prev = 0
  for (let h = 0; h < 24; h++) {
    if (lastPerHour[h] != null) {
      hourly[h] = Math.max(0, lastPerHour[h] - prev)
      prev = lastPerHour[h]
    } else {
      hourly[h] = 0
    }
  }

  // ===== 最近 7 天每日总量（每天最后一条累计值 = 当天总步数）=====
  const daily7 = new Array(7).fill(0)
  const daily7Dates = []
  for (const r of withSteps) {
    const dayIdx = Math.floor((r.ts - start7) / DAY_MS)
    if (dayIdx >= 0 && dayIdx < 7) daily7[dayIdx] = Number(r.steps)
  }
  for (let i = 0; i < 7; i++) {
    const d = new Date(start7 + i * DAY_MS)
    daily7Dates.push(`${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`)
  }

  // 今日总览
  const last = withSteps.length ? withSteps[withSteps.length - 1] : null
  const totalSteps = last ? Number(last.steps) || 0 : 0
  const dist = last && last.dist != null ? Number(last.dist) : 0
  const cal = last && last.cal != null ? Number(last.cal) : 0

  return {
    ok: true,
    date: `${startOfToday.getFullYear()}-${String(startOfToday.getMonth() + 1).padStart(2, "0")}-${String(startOfToday.getDate()).padStart(2, "0")}`,
    totalSteps,
    dist,
    cal,
    hourly,          // 24 个元素，每小时增量步数
    daily7,          // 7 个元素，每日总步数（今天在最后）
    daily7Dates,     // 7 个日期标签 MM-DD
    recordCount: withSteps.length
  }
}
