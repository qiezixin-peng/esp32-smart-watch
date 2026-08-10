// pages/sport/sport.js - 运动：今日总览 + 当天24小时步数柱状图 + 最近7天对比
// 数据来源：sportReport 云函数（云端聚合：24h用增量，7天用每日总量）

// 每日步数目标（可改：与手表端一致 1000）
const GOAL_STEPS = 1000

Page({
  data: {
    loading: true,
    empty: false,
    dateText: "",
    // 今日总览
    totalSteps: 0,
    dist: 0,
    cal: 0,
    goal: GOAL_STEPS,
    goalPercent: 0,
    // 24 小时柱状图
    hourly: [],
    maxHour: 1,
    // 最近 7 天
    daily7: [],
    daily7Dates: [],
    maxDay7: 1,
    countText: ""
  },

  onLoad() { this.loadReport() },
  onReady() { this.drawAll() },

  async loadReport() {
    this.setData({ loading: true, empty: false })
    try {
      // 先采集一次最新数据（让报表包含刚走的步数；失败不影响查看历史）
      try { await wx.cloud.callFunction({ name: "collectSensorData" }) } catch (e) { /* ignore */ }
      const res = await wx.cloud.callFunction({ name: "sportReport" })
      const r = res.result || {}
      if (!r.ok || !r.hourly) {
        this.setData({ loading: false, empty: true })
        return
      }
      const total = Number(r.totalSteps) || 0
      const pct = Math.min(100, Math.round(total / GOAL_STEPS * 100))
      const d7 = r.daily7 || []
      this.setData({
        loading: false,
        empty: false,
        dateText: r.date || "",
        totalSteps: total,
        dist: Number(r.dist) || 0,
        cal: Number(r.cal) || 0,
        goalPercent: pct,
        hourly: r.hourly,
        maxHour: Math.max(1, ...r.hourly),
        daily7: d7,
        daily7Dates: r.daily7Dates || [],
        maxDay7: Math.max(1, ...d7),
        countText: r.recordCount ? `已采集 ${r.recordCount} 条运动数据` : ""
      })
      this.drawAll()
    } catch (e) {
      console.error("读取运动数据失败", e)
      this.setData({ loading: false, empty: true })
    }
  },

  // 数据就绪后画两张图
  drawAll() { this.drawChart(); this.drawChart7() },

  // 画 24 小时步数柱状图（增量）
  async drawChart() {
    const hourly = this.data.hourly
    if (!hourly || hourly.length !== 24) return
    await this.drawBars("#chart", hourly, 24, "各时段步数（增量）", (h) => h % 3 === 0 ? String(h) : "", new Date().getHours())
  },

  // 画最近 7 天柱状图（每日总量，今天高亮）
  async drawChart7() {
    const daily7 = this.data.daily7
    if (!daily7 || daily7.length !== 7) return
    await this.drawBars("#chart7", daily7, 7, "最近 7 天步数", (i) => (this.data.daily7Dates[i] || ""), null)
  },

  // 通用柱状图画布
  async drawBars(canvasId, values, count, title, labelFn, highlightIdx) {
    const query = wx.createSelectorQuery()
    const node = await new Promise((resolve) => {
      query.select(canvasId).fields({ node: true, size: true }).exec((res) => {
        if (res && res[0]) resolve(res[0]); else resolve(null)
      })
    })
    if (!node) return

    const canvas = node.node
    const ctx = canvas.getContext("2d")
    const dpr = wx.getSystemInfoSync().pixelRatio
    canvas.width = node.width * dpr
    canvas.height = node.height * dpr
    ctx.scale(dpr, dpr)

    const W = node.width
    const H = node.height
    // 左边留出 Y 轴数值空间，顶部留出柱顶数值空间
    const padL = 46, padR = 8, padT = 36, padB = 34
    const innerW = W - padL - padR
    const innerH = H - padT - padB

    ctx.clearRect(0, 0, W, H)

    // 图表数值范围：24h 图用当天每小时的最大值；7 天图用 7 天的最大值
    const maxV = count === 24 ? this.data.maxHour : this.data.maxDay7

    // 画横向网格线 + Y 轴刻度数值（顶部=最大值，底部=0），让纵向能读出数据
    ctx.strokeStyle = "#e6ebf3"
    ctx.lineWidth = 1
    ctx.font = "10px sans-serif"
    ctx.fillStyle = "#57606f"
    for (let g = 0; g <= 3; g++) {
      const gy = padT + (innerH * g) / 3
      ctx.beginPath(); ctx.moveTo(padL, gy); ctx.lineTo(W - padR, gy); ctx.stroke()
      const val = Math.round(maxV * (1 - g / 3))
      ctx.textAlign = "right"
      ctx.fillText(String(val), padL - 6, gy + 3)
    }

    // 柱体布局参数
    const slot = innerW / count
    const barW = count === 24 ? Math.max(3, slot * 0.55) : Math.min(44, slot * 0.5)
    const color = "#10b981"

    for (let i = 0; i < count; i++) {
      const v = Number(values[i]) || 0
      const barH = (v / maxV) * innerH
      const x = padL + i * slot + (slot - barW) / 2
      const y = padT + innerH - barH
      // 高亮当前小时/今天，其他半透明
      const hl = highlightIdx != null && i === highlightIdx
      ctx.globalAlpha = hl ? 1 : 0.55
      ctx.fillStyle = hl ? "#059669" : color
      ctx.fillRect(x, y, barW, barH)
      if (barH < 2) {
        ctx.fillStyle = "#d6dde8"
        ctx.fillRect(x, padT + innerH - 2, barW, 2)
      }
      ctx.globalAlpha = 1
      // X 轴刻度标签（24h 每 3 小时；7 天全部标）
      const label = labelFn(i)
      if (label) {
        ctx.fillStyle = "#9aa4b5"
        ctx.font = "9px sans-serif"
        ctx.textAlign = "center"
        ctx.fillText(label, x + barW / 2, H - 10)
      }
      // 柱顶数值：7 天图每根柱子都标；24h 图只标当前小时那根，避免挤成一团
      const showVal = count === 7 || hl
      if (showVal && v > 0) {
        ctx.fillStyle = hl ? "#059669" : "#57606f"
        ctx.font = count === 7 ? "10px sans-serif" : "9px sans-serif"
        ctx.textAlign = "center"
        ctx.fillText(String(v), x + barW / 2, y - 4)
      }
    }

    ctx.textAlign = "left"
    ctx.fillStyle = "#7a8499"
    ctx.font = "12px sans-serif"
    ctx.fillText(title, padL, 16)
  }
})