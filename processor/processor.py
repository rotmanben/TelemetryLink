import os
import json
import time
import logging
import threading
from datetime import datetime, timedelta
from collections import defaultdict, deque
import dash
from dash import dcc, html, Input, Output
import plotly.graph_objs as go
import plotly.express as px
import zmq

# Global data storage for sensor readings
sensor_data = defaultdict(lambda: {
    'timestamps': deque(maxlen=100),
    'cpu_usage': deque(maxlen=100),
    'disk_usage': deque(maxlen=100),
    'status': 'Unknown',
    'last_update': None,
    'corruption_count': 0,
    'total_readings': 0,
    'data_consistent': True
})

def process_incoming_data():
    logging.info("Starting incoming data processor...")

    # Implement a server for receiving data from sensors
    ctx = zmq.Context.instance()
    server = ctx.socket(zmq.REP)
    server.bind("tcp://0.0.0.0:5555")

    try:
        logging.info("Processor listening")
        logging.info("Waiting for sensor to connect...")
        logging.info("Connection established with sensor.")
    except Exception as e:
        logging.error(f"Failed to establish connection: {e}")
        return

    while True:
        try:
            # Receive JSON from ZeroMQ (no data/buffer)
            message = server.recv_json()
            logging.info(f"Received message: {message}")

            # Store data for dashboard
            sensor_id = message["sensor_id"]
            timestamp = datetime.now()

            sensor_data[sensor_id]['timestamps'].append(timestamp)
            sensor_data[sensor_id]['last_update'] = timestamp
            sensor_data[sensor_id]['total_readings'] += 1

            # Check for data consistency
            data_consistent = message.get("data_consistent", True)
            sensor_data[sensor_id]['data_consistent'] = data_consistent

            if not data_consistent:
                sensor_data[sensor_id]['corruption_count'] += 1
                logging.warning(f"Data corruption detected for sensor {sensor_id}")

            # Handle different sensor types
            if "cpu_usage_percent" in message:
                cpu_usage = message.get("cpu_usage_percent", 0)
                sensor_data[sensor_id]['cpu_usage'].append(cpu_usage)
                sensor_data[sensor_id]['status'] = "ALERT" if cpu_usage > 80 else "OK"
            elif "disk_usage_percent" in message:
                disk_usage = message.get("disk_usage_percent", 0)
                sensor_data[sensor_id]['disk_usage'].append(disk_usage)
                sensor_data[sensor_id]['status'] = "ALERT" if disk_usage > 80 else "OK"

            # Send response back via ZMQ
            response = {
                "sensor_id": sensor_id,
                "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "status": sensor_data[sensor_id]['status']
            }
            server.send_json(response)

        except Exception as e:
            logging.error(f"connection error: {e}")
            break

    try:
        logging.info("Cleaning up connection...")
    except Exception as e:
        logging.error(f"Error cleaning up connection: {e}")

def create_dash_app():
    """Create and configure the Dash application"""
    app = dash.Dash(__name__)
    
    app.layout = html.Div([
        html.H1("Gita Linux Expert Software Developer Exercise", style={'textAlign': 'center', 'marginBottom': 30}),
        
        html.Div(id='sensor-cards', style={'marginBottom': 30}),
        
        html.Div([
            html.Div([
                html.H3("CPU Usage Trends"),
                dcc.Graph(id='cpu-usage-graph')
            ], style={'width': '48%', 'display': 'inline-block', 'verticalAlign': 'top'}),
            
            html.Div([
                html.H3("Disk Usage Trends"),
                dcc.Graph(id='disk-usage-graph')
            ], style={'width': '48%', 'display': 'inline-block', 'marginLeft': '4%', 'verticalAlign': 'top'})
        ], style={'width': '100%'}),
        
        html.Div([
            html.H3("Sensor Status Overview"),
            dcc.Graph(id='status-pie-chart')
        ]),
        
        # Auto-refresh component
        dcc.Interval(
            id='interval-component',
            interval=1*1000,  # Update every 1 second
            n_intervals=0
        )
    ], style={'padding': '20px'})
    
    @app.callback(
        [Output('sensor-cards', 'children'),
         Output('cpu-usage-graph', 'figure'),
         Output('disk-usage-graph', 'figure'),
         Output('status-pie-chart', 'figure')],
        [Input('interval-component', 'n_intervals')]
    )
    def update_dashboard(n):
        # Create sensor cards
        cards = []
        for sensor_id, data in sensor_data.items():
            if data['timestamps']:
                status = data['status']
                last_update = data['last_update']
                corruption_rate = (data['corruption_count'] / data['total_readings'] * 100) if data['total_readings'] > 0 else 0
                
                # Determine card color based on corruption and status
                if data['corruption_count'] > 0:
                    card_color = '#ff8800'  # Orange for corruption
                elif status == 'ALERT':
                    card_color = '#ff4444'  # Red for alerts
                else:
                    card_color = '#44ff44'  # Green for OK
                
                if sensor_id.startswith('cpu'):
                    current_value = data['cpu_usage'][-1] if data['cpu_usage'] else 'N/A'
                    value_text = f"CPU Usage: {current_value}%"
                else:
                    current_value = data['disk_usage'][-1] if data['disk_usage'] else 'N/A'
                    value_text = f"Disk Usage: {current_value}%"
                
                card = html.Div([
                    html.H4(f"Sensor {sensor_id}"),
                    html.P(value_text, style={'fontSize': '20px', 'fontWeight': 'bold'}),
                    html.P(f"Status: {status}"),
                    html.P(f"Corruption: {data['corruption_count']}/{data['total_readings']} ({corruption_rate:.1f}%)", 
                           style={'color': 'red' if data['corruption_count'] > 0 else 'green'}),
                    html.P(f"Last Update: {last_update.strftime('%H:%M:%S') if last_update else 'N/A'}")
                ], style={
                    'border': f'2px solid {card_color}',
                    'borderRadius': '10px',
                    'padding': '15px',
                    'margin': '10px',
                    'backgroundColor': '#f9f9f9',
                    'display': 'inline-block',
                    'minWidth': '250px'
                })
                cards.append(card)
        logging.info("creating CPU usage trend graph")
        # Create CPU usage trend graph
        fig_cpu = go.Figure()
        for sensor_id, data in sensor_data.items():
            if data['cpu_usage'] and sensor_id.startswith('cpu'):
                fig_cpu.add_trace(go.Scatter(
                    x=list(data['timestamps']),
                    y=list(data['cpu_usage']),
                    mode='lines+markers',
                    name=f'Sensor {sensor_id}',
                    line=dict(width=2)
                ))
        
        fig_cpu.update_layout(
            title="CPU Usage Over Time",
            xaxis_title="Time",
            yaxis_title="CPU Usage (%)",
            hovermode='closest'
        )
        logging.info("creating disk usage trend graph")
        # Create disk usage trend graph
        fig_disk = go.Figure()
        for sensor_id, data in sensor_data.items():
            if data['disk_usage'] and sensor_id.startswith('disk'):
                fig_disk.add_trace(go.Scatter(
                    x=list(data['timestamps']),
                    y=list(data['disk_usage']),
                    mode='lines+markers',
                    name=f'Sensor {sensor_id}',
                    line=dict(width=2)
                ))
        
        fig_disk.update_layout(
            title="Disk Usage Over Time",
            xaxis_title="Time",
            yaxis_title="Disk Usage (%)",
            hovermode='closest'
        )
        logging.info("creating pie chart for sensor status")
        # Create status pie chart
        status_counts = {'OK': 0, 'ALERT': 0}
        for data in sensor_data.values():
            if data['status'] in status_counts:
                status_counts[data['status']] += 1
        
        fig_pie = px.pie(
            values=list(status_counts.values()),
            names=list(status_counts.keys()),
            title="Sensor Status Distribution",
            color_discrete_map={'OK': '#44ff44', 'ALERT': '#ff4444'}
        )
        logging.info("returning updated layout with sensor data")
        logging.info(f"Dashboard update data: cards={len(cards)}, cpu_traces={len(fig_cpu.data)}, disk_traces={len(fig_disk.data)}, status_counts={status_counts}")
        for i, card_data in enumerate(cards):
            logging.debug(f"Card {i}: {card_data}")
        logging.debug(f"CPU figure data: {fig_cpu.data}")
        logging.debug(f"Disk figure data: {fig_disk.data}")
        logging.debug(f"Pie chart data: {fig_pie.data}")
        return cards, fig_cpu, fig_disk, fig_pie
    logging.info("nothing to update, returning existing layout")
    return app

def main():
    logging.info("Starting processor with web dashboard...")
    
    
    threading.Thread(target=process_incoming_data, daemon=True).start()
    
    # Create and run Dash app
    app = create_dash_app()
    logging.info("Starting web dashboard on http://0.0.0.0:8050")
    #app.run(host='0.0.0.0', port=8050, debug=True)
    app.run(host='0.0.0.0', port=8050, debug=False, use_reloader=False)

if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
    try:
        main()
    finally:
        logging.info("Exiting processor.")
