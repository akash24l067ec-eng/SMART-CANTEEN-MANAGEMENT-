const http = require('http');
const fs = require('fs');
const path = require('path');

const dataFile = path.join(__dirname, 'data.json');
const publicDir = path.join(__dirname, 'public');

const defaultData = {
  users: [],
  menus: [],
  orders: [],
  nextOrderId: 1,
  nextMenuId: 1,
};

function loadData() {
  try {
    const raw = fs.readFileSync(dataFile, 'utf8');
    const parsed = JSON.parse(raw);
    return {
      ...defaultData,
      ...parsed,
      users: Array.isArray(parsed.users) ? parsed.users : [],
      menus: Array.isArray(parsed.menus) ? parsed.menus : [],
      orders: Array.isArray(parsed.orders) ? parsed.orders : [],
    };
  } catch (error) {
    return { ...defaultData };
  }
}

let data = loadData();

function saveData() {
  fs.writeFileSync(dataFile, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
}

function sendJson(res, statusCode, payload) {
  res.writeHead(statusCode, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(payload));
}

function sendText(res, statusCode, message) {
  res.writeHead(statusCode, { 'Content-Type': 'text/plain' });
  res.end(message);
}

function parseJsonBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', chunk => {
      body += chunk;
      if (body.length > 1e6) {
        reject(new Error('Payload too large'));
        req.destroy();
      }
    });
    req.on('end', () => {
      if (!body) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(body));
      } catch (error) {
        reject(error);
      }
    });
  });
}

function isNonEmptyString(value) {
  return typeof value === 'string' && value.trim().length > 0;
}

function toNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function findMenu(menuId) {
  return data.menus.find(menu => menu.id === menuId);
}

function buildOrderItems(items) {
  const enrichedItems = [];
  for (const item of items) {
    const menuId = Number(item.menuId);
    const quantity = toNumber(item.quantity);
    if (!Number.isInteger(menuId) || quantity <= 0) {
      throw new Error('Invalid order items');
    }
    const menu = findMenu(menuId);
    if (!menu) {
      throw new Error('Menu item not found');
    }
    if (menu.quantity < quantity) {
      throw new Error(`Not enough quantity for ${menu.name}`);
    }
    enrichedItems.push({
      menuId: menu.id,
      name: menu.name,
      price: menu.price,
      quantity,
    });
  }
  return enrichedItems;
}

function calculateTotal(items) {
  return items.reduce((sum, item) => sum + item.price * item.quantity, 0);
}

function handleUsers(req, res) {
  if (req.method === 'GET') {
    sendJson(res, 200, data.users);
    return;
  }
  if (req.method === 'POST') {
    parseJsonBody(req)
      .then(body => {
        const uid = isNonEmptyString(body.uid) ? body.uid.trim() : null;
        const name = isNonEmptyString(body.name) ? body.name.trim() : null;
        if (!uid || !name) {
          sendJson(res, 400, { error: 'uid and name are required' });
          return;
        }
        if (data.users.some(user => user.uid === uid)) {
          sendJson(res, 409, { error: 'uid already exists' });
          return;
        }
        const user = {
          uid,
          name,
          balance: toNumber(body.balance),
          credit: toNumber(body.credit),
        };
        data.users.push(user);
        saveData();
        sendJson(res, 201, user);
      })
      .catch(() => sendJson(res, 400, { error: 'Invalid JSON payload' }));
    return;
  }
  sendText(res, 405, 'Method Not Allowed');
}

function handleMenus(req, res) {
  if (req.method === 'GET') {
    sendJson(res, 200, data.menus);
    return;
  }
  if (req.method === 'POST') {
    parseJsonBody(req)
      .then(body => {
        const name = isNonEmptyString(body.name) ? body.name.trim() : null;
        const price = toNumber(body.price, null);
        const quantity = toNumber(body.quantity, null);
        if (!name || price === null || quantity === null) {
          sendJson(res, 400, { error: 'name, price, and quantity are required' });
          return;
        }
        const menuItem = {
          id: data.nextMenuId++,
          name,
          price,
          quantity,
        };
        data.menus.push(menuItem);
        saveData();
        sendJson(res, 201, menuItem);
      })
      .catch(() => sendJson(res, 400, { error: 'Invalid JSON payload' }));
    return;
  }
  sendText(res, 405, 'Method Not Allowed');
}

function handleVerify(req, res) {
  if (req.method !== 'POST') {
    sendText(res, 405, 'Method Not Allowed');
    return;
  }
  parseJsonBody(req)
    .then(body => {
      const uid = isNonEmptyString(body.uid) ? body.uid.trim() : null;
      if (!uid) {
        sendJson(res, 400, { error: 'uid is required' });
        return;
      }
      const user = data.users.find(entry => entry.uid === uid);
      if (!user) {
        sendJson(res, 200, { access: false, message: 'Access denied' });
        return;
      }
      sendJson(res, 200, {
        access: true,
        user,
        menus: data.menus,
      });
    })
    .catch(() => sendJson(res, 400, { error: 'Invalid JSON payload' }));
}

function handleOrders(req, res) {
  if (req.method === 'GET') {
    sendJson(res, 200, data.orders);
    return;
  }
  if (req.method === 'POST') {
    parseJsonBody(req)
      .then(body => {
        const uid = isNonEmptyString(body.uid) ? body.uid.trim() : null;
        const items = Array.isArray(body.items) ? body.items : [];
        const user = data.users.find(entry => entry.uid === uid);
        if (!uid || !user) {
          sendJson(res, 400, { error: 'Valid uid is required' });
          return;
        }
        if (!items.length) {
          sendJson(res, 400, { error: 'Order items are required' });
          return;
        }
        let enrichedItems;
        try {
          enrichedItems = buildOrderItems(items);
        } catch (error) {
          sendJson(res, 400, { error: error.message });
          return;
        }
        const total = calculateTotal(enrichedItems);
        if (user.balance < total) {
          sendJson(res, 400, { error: 'Insufficient balance' });
          return;
        }
        enrichedItems.forEach(item => {
          const menu = findMenu(item.menuId);
          menu.quantity -= item.quantity;
        });
        user.balance -= total;
        const order = {
          id: data.nextOrderId++,
          uid,
          userName: user.name,
          items: enrichedItems,
          total,
          status: 'placed',
          createdAt: new Date().toISOString(),
        };
        data.orders.unshift(order);
        saveData();
        sendJson(res, 201, order);
      })
      .catch(() => sendJson(res, 400, { error: 'Invalid JSON payload' }));
    return;
  }
  sendText(res, 405, 'Method Not Allowed');
}

function handleOrderAction(req, res, orderId, action) {
  const order = data.orders.find(entry => entry.id === orderId);
  if (!order) {
    sendJson(res, 404, { error: 'Order not found' });
    return;
  }
  if (action === 'ready') {
    order.status = 'ready';
  } else if (action === 'cancel') {
    if (order.status !== 'cancelled') {
      const user = data.users.find(entry => entry.uid === order.uid);
      if (user) {
        user.balance += order.total;
      }
      order.items.forEach(item => {
        const menu = findMenu(item.menuId);
        if (menu) {
          menu.quantity += item.quantity;
        }
      });
    }
    order.status = 'cancelled';
  }
  saveData();
  sendJson(res, 200, order);
}

function getContentType(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  switch (ext) {
    case '.html':
      return 'text/html';
    case '.css':
      return 'text/css';
    case '.js':
      return 'application/javascript';
    case '.json':
      return 'application/json';
    case '.png':
      return 'image/png';
    case '.svg':
      return 'image/svg+xml';
    default:
      return 'text/plain';
  }
}

function serveStatic(req, res, pathname) {
  const safePath = pathname === '/' ? '/index.html' : pathname;
  const decoded = decodeURIComponent(safePath);
  const filePath = path.join(publicDir, decoded);
  if (!filePath.startsWith(publicDir)) {
    sendText(res, 400, 'Invalid path');
    return;
  }
  fs.readFile(filePath, (err, content) => {
    if (err) {
      sendText(res, 404, 'Not Found');
      return;
    }
    res.writeHead(200, { 'Content-Type': getContentType(filePath) });
    res.end(content);
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const pathname = url.pathname;

  if (pathname.startsWith('/api/')) {
    if (pathname === '/api/users') {
      handleUsers(req, res);
      return;
    }
    if (pathname === '/api/menus') {
      handleMenus(req, res);
      return;
    }
    if (pathname === '/api/orders') {
      handleOrders(req, res);
      return;
    }
    if (pathname === '/api/verify') {
      handleVerify(req, res);
      return;
    }
    const orderMatch = pathname.match(/^\/api\/orders\/(\d+)\/(ready|cancel)$/);
    if (orderMatch) {
      const orderId = Number(orderMatch[1]);
      if (req.method !== 'POST') {
        sendText(res, 405, 'Method Not Allowed');
        return;
      }
      handleOrderAction(req, res, orderId, orderMatch[2]);
      return;
    }
    sendText(res, 404, 'Not Found');
    return;
  }

  if (req.method !== 'GET') {
    sendText(res, 405, 'Method Not Allowed');
    return;
  }
  serveStatic(req, res, pathname);
});

const port = process.env.PORT || 3000;
server.listen(port, () => {
  console.log(`Smart Canteen server running at http://localhost:${port}`);
});
