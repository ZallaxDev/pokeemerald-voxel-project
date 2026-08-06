from fastapi import APIRouter, HTTPException, UploadFile, File
from app.models.project import ProjectData
import json

router = APIRouter(prefix="/api/project", tags=["project"])

@router.post("/save")
async def save_project(data: ProjectData):
    try:
        # Return validated JSON project payload
        return data.model_dump()
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.post("/load")
async def load_project(file: UploadFile = File(...)):
    try:
        content = await file.read()
        json_dict = json.loads(content.decode("utf-8"))
        project = ProjectData(**json_dict)
        return project.model_dump()
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Invalid project file: {str(e)}")
