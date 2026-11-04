// Печатает HTML-мануал в PDF через Chromium (Playwright): node print.js <html> <pdf>
const path = require('path');
const { chromium } = require('playwright');

(async () => {
  const [, , html, pdf] = process.argv;
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + path.resolve(html), { waitUntil: 'networkidle' });
  await page.evaluate(() => document.fonts.ready);
  await page.pdf({ path: pdf, format: 'A4', printBackground: true, preferCSSPageSize: true });
  await browser.close();
})();
