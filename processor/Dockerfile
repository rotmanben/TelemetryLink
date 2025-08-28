FROM python:3

# Install required packages
RUN pip install dash plotly pandas pyzmq

COPY processor.py .

# Expose port for web dashboard
EXPOSE 8050

CMD ["python", "processor.py"]
