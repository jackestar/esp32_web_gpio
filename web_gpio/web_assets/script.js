let svgLoaded = false;
let svgDoc;

async function fetchState() {
    try {
        const res = await fetch('/api/state');
        const data = await res.json();
        render(data);
    } catch (e) {
        console.error("Failed to fetch state");
    }
}

function render(data) {
    const container = document.getElementById('gpio-container');
    if (!svgLoaded) {
        fetch('esp32-devkitv1-30.svg')
            .then(response => response.text())
            .then(svgText => {
                container.innerHTML = svgText;
                svgDoc = container.querySelector('svg');
                svgLoaded = true;
                updateSVG(data);
            })
            .catch(e => console.error('Failed to load SVG', e));
    } else {
        updateSVG(data);
    }
}

function updateSVG(data) {
    data.forEach(gpio => {
        const group = svgDoc.getElementById(`gpio_${gpio.pin}`);
        if (group) {
            const paths = group.querySelectorAll('path');
            if (paths.length >= 2) {
                // First path: level indicator
                const levelFill = gpio.val ? '#f60' : '#000';
                paths[0].style.fill = levelFill;

                // Second path: direction indicator
                paths[1].style.fill = gpio.dir === 'in' ? '#06f' : '#4a0';

                // Level toggle on first path
                paths[0].style.cursor = gpio.dir === 'out' ? 'pointer' : 'default';
                paths[0].onclick = (evt) => {
                    evt.stopPropagation();
                    if (gpio.dir === 'out') {
                        toggleLevel(gpio.pin, gpio.val ? 0 : 1);
                    }
                };

                // Direction toggle on second path
                paths[1].style.cursor = 'pointer';
                paths[1].onclick = (evt) => {
                    evt.stopPropagation();
                    toggleMode(gpio.pin, gpio.dir === 'in' ? 'out' : 'in');
                };
            }
            const textEl = svgDoc.querySelector(`#text_gpio_${gpio.pin} tspan tspan`);
            if (textEl) {
                textEl.textContent = gpio.dir.toUpperCase();
            }
            const textDir = svgDoc.querySelector(`#gpio_${gpio.pin} text tspan`);
            if (textDir)
                textDir.style.fill = gpio.val ? '#000' : '#fff';

            group.style.cursor = 'default';
            group.onclick = null;
        }
    });
}

function toggleMode(pin, mode) {
    fetch(`/api/mode?pin=${pin}&mode=${mode}`).then(() => fetchState());
}

function toggleLevel(pin, level) {
    fetch(`/api/level?pin=${pin}&level=${level}`).then(() => fetchState());
}

fetchState();

setInterval(fetchState, 500);