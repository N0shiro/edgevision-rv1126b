# 模型目录说明

建议将板端部署所需模型和标签文件统一放在该目录或其镜像目录中：

```text
models/
├─ yolov5n.rknn
└─ coco_80_labels.txt
```

推荐配合 `config/aicam.env.example` 中的环境变量一起使用：

- `AICAM_RKNN_MODEL`
- `AICAM_LABELS`
