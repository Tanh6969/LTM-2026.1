import { useState, useEffect } from "react"; 
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Label } from "@/components/ui/label";
import { RadioGroup, RadioGroupItem } from "@/components/ui/radio-group";
import { Slider } from "@/components/ui/slider";

export function FlightSideFilter({ filters, setFilters }) {
  const [localBudget, setLocalBudget] = useState(filters.budget);
  const [localDepartureTime, setLocalDepartureTime] = useState(filters.departureTime);
  
  // Thêm một state để kiểm tra xem component đã "mount" (lên trình duyệt) chưa
  const [mounted, setMounted] = useState(false);

  useEffect(() => {
    setMounted(true);
  }, []);

  const handleApplyFilters = () => {
    setFilters((prev) => ({
      ...prev,
      budget: localBudget,
      departureTime: localDepartureTime,
    }));
  };

  const handleResetFilters = () => {
    const defaultBudget = [100000, 4000000]; // Đồng bộ với min/max của Slider
    const defaultDepartureTime = "all";
    setLocalBudget(defaultBudget);
    setLocalDepartureTime(defaultDepartureTime);
    setFilters({
      budget: defaultBudget,
      departureTime: defaultDepartureTime,
    });
  };

  // Hàm helper để format tiền tệ an toàn, tránh lệch chuẩn Server/Client
  const formatCurrency = (value) => {
    if (!mounted) return ""; // Trả về rỗng khi render trên server để tránh mismatch
    return value.toLocaleString('vi-VN'); // Ép kiểu format tiếng Việt (dấu chấm)
  };

  return (
    <Card className="w-full md:w-64 h-fit">
      <CardContent className="p-4">
        <div className="flex justify-between items-center mb-4">
          <h2 className="text-lg font-semibold">Bộ lọc</h2>
        </div>

        {/* Bộ lọc ngân sách */}
        <div className="mb-4">
          <Label>Ngân sách</Label>
          <div className="flex justify-between text-sm mt-2">
            {/* Sửa chỗ này: Dùng hàm formatCurrency đã tạo ở trên */}
            <span>{formatCurrency(localBudget[0])} VND</span>
            <span>{formatCurrency(localBudget[1])} VND</span>
          </div>
          <Slider
            min={100000}
            max={4000000}
            step={1000}
            value={localBudget}
            onValueChange={setLocalBudget}
            className="mt-2"
          />
        </div>

        {/* Bộ lọc giờ khởi hành */}
        <div className="mb-4">
          <Label>Giờ khởi hành</Label>
          <RadioGroup
            value={localDepartureTime}
            onValueChange={setLocalDepartureTime}
          >
            <div className="flex items-center space-x-2 mt-2">
              <RadioGroupItem value="all" id="all" />
              <Label htmlFor="all">Tất cả</Label>
            </div>
            <div className="flex items-center space-x-2 mt-2">
              <RadioGroupItem value="morning" id="morning" />
              <Label htmlFor="morning">00:00 - 11:59 Sáng</Label>
            </div>
            <div className="flex items-center space-x-2 mt-2">
              <RadioGroupItem value="afternoon" id="afternoon" />
              <Label htmlFor="afternoon">12:00 - 17:59 Chiều</Label>
            </div>
            <div className="flex items-center space-x-2 mt-2">
              <RadioGroupItem value="evening" id="evening" />
              <Label htmlFor="evening">18:00 - 23:59 Tối</Label>
            </div>
          </RadioGroup>
        </div>

        <div className="flex justify-between mt-4 gap-2">
          <Button variant="outline" onClick={handleResetFilters} className="px-2 text-xs">
            Thiết lập lại
          </Button>
          <Button className="bg-orange-500 hover:bg-orange-600 text-white px-4 text-xs" onClick={handleApplyFilters}>
            Áp dụng
          </Button>
        </div>
      </CardContent>
    </Card>
  );
}