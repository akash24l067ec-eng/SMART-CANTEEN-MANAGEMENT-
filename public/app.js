const userTableBody = document.getElementById('user-table-body');
const menuTableBody = document.getElementById('menu-table-body');
const ordersContainer = document.getElementById('orders');
const statusEl = document.getElementById('status');

const refreshButton = document.getElementById('refresh');
const userForm = document.getElementById('user-form');
const menuForm = document.getElementById('menu-form');
const REFRESH_INTERVAL_MS = 10000;
ordersContainer.setAttribute('role', 'status');
ordersContainer.setAttribute('aria-live', 'polite');

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status}`);
  }
  return response.json();
}

async function postJson(url, payload) {
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || 'Request failed');
  }
  return data;
}

function setStatus(message, isError = false) {
  statusEl.textContent = message;
  statusEl.style.color = isError ? '#dc2626' : '#2563eb';
}

function createCell(text) {
  const cell = document.createElement('td');
  cell.textContent = text;
  return cell;
}

function renderTableRows(container, rows, renderer) {
  container.replaceChildren();
  rows.forEach(row => container.appendChild(renderer(row)));
}

function renderUsers(users) {
  renderTableRows(userTableBody, users, user => {
    const row = document.createElement('tr');
    row.appendChild(createCell(user.uid));
    row.appendChild(createCell(user.name));
    row.appendChild(createCell(`₹${user.balance.toFixed(2)}`));
    row.appendChild(createCell(`₹${user.credit.toFixed(2)}`));
    return row;
  });
}

function renderMenus(menus) {
  renderTableRows(menuTableBody, menus, menu => {
    const row = document.createElement('tr');
    row.appendChild(createCell(menu.name));
    row.appendChild(createCell(`₹${menu.price.toFixed(2)}`));
    row.appendChild(createCell(String(menu.quantity)));
    return row;
  });
}

function renderOrders(orders) {
  ordersContainer.replaceChildren();
  if (!orders.length) {
    ordersContainer.textContent = 'No active orders yet.';
    return;
  }
  orders.forEach(order => {
    const card = document.createElement('div');
    card.className = 'order-card';
    const itemsList = order.items
      .map(item => `${item.name} × ${item.quantity}`)
      .join(', ');
    const title = document.createElement('strong');
    title.textContent = `Order #${order.id}`;
    const userLine = document.createElement('div');
    userLine.textContent = `User: ${order.userName}`;
    const itemsLine = document.createElement('div');
    itemsLine.textContent = `Items: ${itemsList}`;
    const totalLine = document.createElement('div');
    totalLine.textContent = `Total: ₹${order.total.toFixed(2)}`;
    const statusLine = document.createElement('div');
    statusLine.textContent = `Status: ${order.status}`;
    card.appendChild(title);
    card.appendChild(userLine);
    card.appendChild(itemsLine);
    card.appendChild(totalLine);
    card.appendChild(statusLine);
    if (order.status === 'placed') {
      const actions = document.createElement('div');
      actions.className = 'order-actions';

      const readyButton = document.createElement('button');
      readyButton.textContent = 'Ready';
      readyButton.addEventListener('click', async () => {
        try {
          const updated = await postJson(`/api/orders/${order.id}/ready`, {});
          announceReady(updated);
          await refreshAll();
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      const cancelButton = document.createElement('button');
      cancelButton.textContent = 'Cancel';
      cancelButton.className = 'danger';
      cancelButton.addEventListener('click', async () => {
        try {
          await postJson(`/api/orders/${order.id}/cancel`, {});
          await refreshAll();
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      actions.appendChild(readyButton);
      actions.appendChild(cancelButton);
      card.appendChild(actions);
    }
    ordersContainer.appendChild(card);
  });
}

function announceReady(order) {
  if ('speechSynthesis' in window) {
    const utterance = new SpeechSynthesisUtterance(
      `Order for ${order.userName} is ready for pickup.`
    );
    window.speechSynthesis.speak(utterance);
  }
}

async function refreshAll() {
  try {
    const [users, menus, orders] = await Promise.all([
      fetchJson('/api/users'),
      fetchJson('/api/menus'),
      fetchJson('/api/orders'),
    ]);
    renderUsers(users);
    renderMenus(menus);
    renderOrders(orders);
    setStatus('Dashboard refreshed');
  } catch (error) {
    setStatus(error.message, true);
  }
}

userForm.addEventListener('submit', async event => {
  event.preventDefault();
  const formData = new FormData(userForm);
  try {
    await postJson('/api/users', {
      uid: formData.get('uid'),
      name: formData.get('name'),
      balance: formData.get('balance'),
      credit: formData.get('credit'),
    });
    userForm.reset();
    await refreshAll();
  } catch (error) {
    setStatus(error.message, true);
  }
});

menuForm.addEventListener('submit', async event => {
  event.preventDefault();
  const formData = new FormData(menuForm);
  try {
    await postJson('/api/menus', {
      name: formData.get('name'),
      price: formData.get('price'),
      quantity: formData.get('quantity'),
    });
    menuForm.reset();
    await refreshAll();
  } catch (error) {
    setStatus(error.message, true);
  }
});

refreshButton.addEventListener('click', refreshAll);

refreshAll();
setInterval(refreshAll, REFRESH_INTERVAL_MS);
